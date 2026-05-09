#include "power_management.h"

PowerManager::PowerManager() 
    : sleep_enabled(true),
      sleep_enter_time_us(0) {
}

bool PowerManager::begin() {
    INFO_PRINT("Power Manager initializing...");
    
    // Configure GPIOs for wakeup (external interrupts INT1=IO12, INT2=IO14)
    uint64_t wake_mask = (1ULL << GPIO_ADXL345_INT1) | (1ULL << GPIO_ADXL345_INT2);
    esp_sleep_enable_ext1_wakeup(wake_mask, ESP_EXT1_WAKEUP_ALL_LOW);
    
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
