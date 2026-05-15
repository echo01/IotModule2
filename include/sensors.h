#ifndef SENSORS_H
#define SENSORS_H

#include <Wire.h>
#include "config.h"
#include "common.h"

class MEMSSensor {
public:
    static constexpr uint16_t FFT_DISPLAY_POINTS = 120;

    MEMSSensor();
    ~MEMSSensor();
    
    // Initialize I2C and sensor
    bool begin();
    
    // Read raw acceleration data
    bool readRawData(MEMSData& data);
    
    // Process vibration analysis
    bool processVibrationData(const MEMSData& raw_data, VibrationAnalysis& analysis, bool compute_fft);
    
    // Configure sensor
    bool setDataRate(uint16_t rate_hz);
    bool setRange(uint8_t range_g);
    bool setOffset(float offset_x, float offset_y, float offset_z);
    bool setInterruptThreshold(uint16_t threshold_mg);
    
    // Get current settings
    uint16_t getDataRate() const;
    uint8_t getRange() const;
    
    // Interrupt setup
    bool setupInterrupt(uint8_t int_pin, bool activity);
    bool clearInterruptSource(uint8_t* source = nullptr);
    
    // Check if data is available
    bool dataAvailable();
    
    // Self-test
    bool selfTest();

    // Get latest FFT spectrum for selected axis ('x', 'y', 'z')
    bool getFFTSpectrum(char axis, float* frequency_hz, float* amplitude_mm_s,
                        uint16_t max_points, uint16_t& out_points) const;
    void clearFFTSpectrum();

private:
    uint16_t current_rate_hz;
    uint8_t current_range_g;
    uint16_t interrupt_threshold_mg;
    float offset_x, offset_y, offset_z;
    
    float spectrum_freq[FFT_DISPLAY_POINTS];
    float spectrum_x[FFT_DISPLAY_POINTS];
    float spectrum_y[FFT_DISPLAY_POINTS];
    float spectrum_z[FFT_DISPLAY_POINTS];
    uint16_t spectrum_points;

    // Helper functions
    float calculateRMS(const float* data, uint32_t count);
    void calculatePitchRoll(const MEMSData& data, float& pitch, float& roll);
    void performFFT(const float* acceleration_data, uint32_t count,
                   float& peak_frequency, float& power);
    void buildDisplaySpectrum(const float* acceleration_data, uint32_t count, float* spectrum_amplitude_mm_s);
};

class BatterySensor {
public:
    BatterySensor();
    
    // Initialize ADC
    bool begin();
    
    // Read battery voltage
    float readVoltage();

    // Read multiple samples and return the average voltage
    float readVoltageAverage(uint8_t samples, uint32_t delay_ms = 0);
    
    // Get battery percentage (0-100%)
    float getBatteryPercentage();
    
    // Check if battery is low
    bool isBatteryLow();

private:
    static constexpr float MIN_BATTERY_V = 3.0f;
    static constexpr float MAX_BATTERY_V = 4.2f;
};

#endif // SENSORS_H
