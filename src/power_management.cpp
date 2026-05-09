#include "power_management.h"
#include <driver/rtc_io.h>

PowerManager::PowerManager() 
    : sleep_enabled(true),
      sleep_enter_time_us(0) {
}

bool PowerManager::begin() {
    INFO_PRINT("Power Manager initializing...");
    
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
    uint64_t wake_mask = (1ULL << GPIO_ADXL345_INT1);
    esp_sleep_enable_ext1_wakeup(wake_mask, ESP_EXT1_WAKEUP_ALL_LOW);
    configureModeSwitchWakeup();
    
    // Configure timer wakeup
    configureTimerWakeup(DEEP_SLEEP_INTERVAL_SEC);
    
    return true;
}

void PowerManager::enterDeepSleep(uint32_t sleep_sec) {
    if (!sleep_enabled) {
        DEBUG_PRINT("Sleep disabled (debug mode)");
        return;
    }
    
    INFO_PRINT("Entering deep sleep for %u seconds...", sleep_sec);
    configureTimerWakeup(sleep_sec);
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
    DEBUG_PRINT("Timer wakeup configured for %u seconds", seconds);
}

void PowerManager::configureExtIntWakeup(uint8_t pin, bool active_high) {
    int level = active_high ? 1 : 0;
    esp_sleep_enable_ext0_wakeup((gpio_num_t)pin, level);
    DEBUG_PRINT("External interrupt wakeup configured on GPIO %u (active %s)", 
               pin, active_high ? "HIGH" : "LOW");
}

void PowerManager::configureModeSwitchWakeup() {
    const gpio_num_t mode_pin = static_cast<gpio_num_t>(GPIO_MODE_SWITCH);

    rtc_gpio_init(mode_pin);
    rtc_gpio_set_direction(mode_pin, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en(mode_pin);
    rtc_gpio_pulldown_dis(mode_pin);
    esp_sleep_enable_ext0_wakeup(mode_pin, 1);

    DEBUG_PRINT("Mode switch wakeup configured on GPIO %u (active HIGH)", GPIO_MODE_SWITCH);
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
    DEBUG_PRINT("Sleep disabled");
}

void PowerManager::enableSleep() {
    sleep_enabled = true;
    DEBUG_PRINT("Sleep enabled");
}
