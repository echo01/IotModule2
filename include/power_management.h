#ifndef POWER_MANAGEMENT_H
#define POWER_MANAGEMENT_H

#include <esp_sleep.h>
#include "config.h"
#include "common.h"

class PowerManager {
public:
    PowerManager();
    
    // Initialize power management
    bool begin();
    
    // Enter deep sleep
    void enterDeepSleep(uint32_t sleep_sec = DEEP_SLEEP_INTERVAL_SEC);
    
    // Configure timer wakeup
    void configureTimerWakeup(uint32_t seconds);
    
    // Configure external interrupt wakeup
    void configureExtIntWakeup(uint8_t pin, bool active_high = true);

    // Configure mode switch as a debug wake source
    void configureModeSwitchWakeup();
    
    // Get wakeup reason
    WakeupReason getWakeupReason();
    
    // Get sleep duration
    uint64_t getSleepDuration_us();
    
    // Check if sleep is enabled
    bool isSleepEnabled() const;
    
    // Disable sleep for debug mode
    void disableSleep();
    
    // Enable sleep
    void enableSleep();

private:
    bool sleep_enabled;
    uint64_t sleep_enter_time_us;
};

#endif // POWER_MANAGEMENT_H
