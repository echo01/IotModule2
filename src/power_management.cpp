#include "power_management.h"
#include <driver/rtc_io.h>

namespace {
constexpr uint32_t kWakePolicyMagic = 0x574B5031UL;
constexpr uint8_t kSuppressAfterMotionWakeCount = 5;
constexpr uint8_t kResumeAfterTimerWakeCount = 2;

const char* wakePolicyStateString(bool suppressed) {
    return suppressed ? "suppressed" : "enabled";
}

struct WakePolicyRTCState {
    uint32_t magic;
    uint8_t motion_wakeup_streak;
    uint8_t timer_wakeup_streak_after_suppression;
    bool motion_wakeup_suppressed;
};

RTC_DATA_ATTR WakePolicyRTCState g_wake_policy_state = {};
}

PowerManager::PowerManager() 
    : sleep_enabled(true),
      motion_wakeup_suppressed(false),
      sleep_enter_time_us(0) {
}

bool PowerManager::begin(WakeupReason last_wakeup_reason,
                         bool adxl_interrupt_pending_at_boot) {
    INFO_PRINT("Power Manager initializing...");

    updateWakePolicyOnBoot(last_wakeup_reason, adxl_interrupt_pending_at_boot);
    
    // ADXL345 interrupts are active LOW. EXT1 on ESP32 wakes on ALL_LOW, so only arm INT1.
    const gpio_num_t adxl_int1 = static_cast<gpio_num_t>(GPIO_ADXL345_INT1);
    const gpio_num_t adxl_int2 = static_cast<gpio_num_t>(GPIO_ADXL345_INT2);
    rtc_gpio_init(adxl_int1);
    rtc_gpio_set_direction(adxl_int1, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(adxl_int1);
    rtc_gpio_pulldown_dis(adxl_int1);
    rtc_gpio_init(adxl_int2);
    rtc_gpio_set_direction(adxl_int2, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(adxl_int2);
    rtc_gpio_pulldown_dis(adxl_int2);
    configureMotionWakeup(!motion_wakeup_suppressed);
    configureModeSwitchWakeup();
    
    // Configure timer wakeup
    configureTimerWakeup(DEEP_SLEEP_INTERVAL_SEC);
    
    return true;
}

void PowerManager::enterDeepSleep(uint32_t sleep_sec) {
    if (!sleep_enabled) {
        DEBUG_POWER_PRINT("Sleep disabled (debug mode)");
        return;
    }
    
    ALWAYS_INFO_PRINT("Entering deep sleep for %u seconds...", sleep_sec);
    configureTimerWakeup(sleep_sec);
    configureMotionWakeup(!motion_wakeup_suppressed);
    configureModeSwitchWakeup();
    
    // Turn off LED
    digitalWrite(GPIO_LED_STATUS, LOW);
    
    // Store time for later reference
    sleep_enter_time_us = micros();
    
    // Flush UART
    Serial.flush();
    delay(100);
    
    // Enter sleep
    esp_deep_sleep_start();
}

void PowerManager::configureTimerWakeup(uint32_t seconds) {
    esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
    DEBUG_POWER_PRINT("Timer wakeup configured for %u seconds", seconds);
}

void PowerManager::configureMotionWakeup(bool enabled) {
    if (enabled) {
        const uint64_t wake_mask = (1ULL << GPIO_ADXL345_INT1);
        esp_sleep_enable_ext1_wakeup(wake_mask, ESP_EXT1_WAKEUP_ALL_LOW);
        motion_wakeup_suppressed = false;
        ALWAYS_INFO_PRINT("Motion INT wakeup enabled (Timer + INT)");
    } else {
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_EXT1);
        motion_wakeup_suppressed = true;
        ALWAYS_INFO_PRINT("Motion INT wakeup suppressed (Timer only)");
    }
}

void PowerManager::configureExtIntWakeup(uint8_t pin, bool active_high) {
    int level = active_high ? 1 : 0;
    esp_sleep_enable_ext0_wakeup((gpio_num_t)pin, level);
    DEBUG_POWER_PRINT("External interrupt wakeup configured on GPIO %u (active %s)", 
               pin, active_high ? "HIGH" : "LOW");
}

void PowerManager::configureModeSwitchWakeup() {
    const gpio_num_t mode_pin = static_cast<gpio_num_t>(GPIO_MODE_SWITCH);

    rtc_gpio_init(mode_pin);
    rtc_gpio_set_direction(mode_pin, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(mode_pin);
    rtc_gpio_pulldown_dis(mode_pin);
    esp_sleep_enable_ext0_wakeup(mode_pin, 1);

    DEBUG_POWER_PRINT("Mode switch wakeup configured on GPIO %u (active HIGH)", GPIO_MODE_SWITCH);
}

WakeupReason PowerManager::getWakeupReason() {
    return get_wakeup_reason();
}

uint64_t PowerManager::getSleepDuration_us() {
    if (sleep_enter_time_us == 0) return 0;
    return micros() - sleep_enter_time_us;
}

bool PowerManager::isSleepEnabled() const {
    return sleep_enabled;
}

void PowerManager::disableSleep() {
    sleep_enabled = false;
    DEBUG_POWER_PRINT("Sleep disabled");
}

void PowerManager::enableSleep() {
    sleep_enabled = true;
    DEBUG_POWER_PRINT("Sleep enabled");
}

bool PowerManager::isMotionWakeupSuppressed() const {
    return motion_wakeup_suppressed;
}

void PowerManager::updateWakePolicyOnBoot(WakeupReason last_wakeup_reason,
                                          bool adxl_interrupt_pending_at_boot) {
    if (g_wake_policy_state.magic != kWakePolicyMagic) {
        g_wake_policy_state.magic = kWakePolicyMagic;
        g_wake_policy_state.motion_wakeup_streak = 0;
        g_wake_policy_state.timer_wakeup_streak_after_suppression = 0;
        g_wake_policy_state.motion_wakeup_suppressed = false;
        INFO_PRINT("Wake policy init: rtc_state reset to defaults");
    }

    const uint8_t motion_streak_before = g_wake_policy_state.motion_wakeup_streak;
    const uint8_t timer_streak_before = g_wake_policy_state.timer_wakeup_streak_after_suppression;
    const bool motion_suppressed_before = g_wake_policy_state.motion_wakeup_suppressed;

    INFO_PRINT("Wake policy pre: last=%s motion_streak=%u/%u timer_streak=%u/%u motion_int=%s adxl_int_boot=%s",
               wakeup_reason_to_string(last_wakeup_reason),
               static_cast<unsigned>(motion_streak_before),
               static_cast<unsigned>(kSuppressAfterMotionWakeCount),
               static_cast<unsigned>(timer_streak_before),
               static_cast<unsigned>(kResumeAfterTimerWakeCount),
               wakePolicyStateString(motion_suppressed_before),
               adxl_interrupt_pending_at_boot ? "pending" : "clear");

    switch (last_wakeup_reason) {
        case WAKE_EXT_INT_MOTION:
            g_wake_policy_state.motion_wakeup_streak++;
            g_wake_policy_state.timer_wakeup_streak_after_suppression = 0;
            INFO_PRINT("Wake policy event: motion wake counted, INT streak -> %u/%u",
                       static_cast<unsigned>(g_wake_policy_state.motion_wakeup_streak),
                       static_cast<unsigned>(kSuppressAfterMotionWakeCount));
            if (g_wake_policy_state.motion_wakeup_streak >= kSuppressAfterMotionWakeCount) {
                g_wake_policy_state.motion_wakeup_suppressed = true;
                INFO_PRINT("Wake policy action: suppress motion INT after %u consecutive motion wakes",
                           static_cast<unsigned>(kSuppressAfterMotionWakeCount));
            }
            break;

        case WAKE_TIMER:
            if (g_wake_policy_state.motion_wakeup_suppressed) {
                if (!adxl_interrupt_pending_at_boot) {
                    g_wake_policy_state.timer_wakeup_streak_after_suppression++;
                    INFO_PRINT("Wake policy event: clean timer wake counted while INT suppressed, timer streak -> %u/%u",
                               static_cast<unsigned>(g_wake_policy_state.timer_wakeup_streak_after_suppression),
                               static_cast<unsigned>(kResumeAfterTimerWakeCount));
                    if (g_wake_policy_state.timer_wakeup_streak_after_suppression >= kResumeAfterTimerWakeCount) {
                        g_wake_policy_state.motion_wakeup_suppressed = false;
                        g_wake_policy_state.motion_wakeup_streak = 0;
                        g_wake_policy_state.timer_wakeup_streak_after_suppression = 0;
                        INFO_PRINT("Wake policy action: re-enable motion INT after %u consecutive clean timer wakes",
                                   static_cast<unsigned>(kResumeAfterTimerWakeCount));
                    }
                } else {
                    // Timer woke the ESP32, but the ADXL345 interrupt line is still active.
                    // Do not count this as an "idle" timer wake toward re-enabling motion wake.
                    g_wake_policy_state.timer_wakeup_streak_after_suppression = 0;
                    INFO_PRINT("Wake policy event: timer wake ignored for resume because ADXL345 INT is still pending");
                }
            } else {
                if (g_wake_policy_state.motion_wakeup_streak != 0 ||
                    g_wake_policy_state.timer_wakeup_streak_after_suppression != 0) {
                    INFO_PRINT("Wake policy event: timer wake broke motion INT streak, counters reset");
                } else {
                    INFO_PRINT("Wake policy event: timer wake while INT already enabled, counters remain idle");
                }
                g_wake_policy_state.motion_wakeup_streak = 0;
                g_wake_policy_state.timer_wakeup_streak_after_suppression = 0;
            }
            break;

        case WAKE_MODE_SWITCH:
        case WAKE_FIRST_BOOT:
        case WAKE_UNKNOWN:
        case WAKE_EXT_INT:
        default:
            INFO_PRINT("Wake policy event: non-motion wake reason resets wake policy counters");
            g_wake_policy_state.motion_wakeup_streak = 0;
            g_wake_policy_state.timer_wakeup_streak_after_suppression = 0;
            g_wake_policy_state.motion_wakeup_suppressed = false;
            break;
    }

    motion_wakeup_suppressed = g_wake_policy_state.motion_wakeup_suppressed;
    INFO_PRINT("Wake policy post: last=%s motion_streak=%u/%u timer_streak=%u/%u motion_int=%s adxl_int_boot=%s",
               wakeup_reason_to_string(last_wakeup_reason),
               static_cast<unsigned>(g_wake_policy_state.motion_wakeup_streak),
               static_cast<unsigned>(kSuppressAfterMotionWakeCount),
               static_cast<unsigned>(g_wake_policy_state.timer_wakeup_streak_after_suppression),
               static_cast<unsigned>(kResumeAfterTimerWakeCount),
               wakePolicyStateString(motion_wakeup_suppressed),
               adxl_interrupt_pending_at_boot ? "pending" : "clear");
}
