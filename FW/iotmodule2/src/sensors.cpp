#include "sensors.h"
#include <math.h>
#include "arduinoFFT.h"

namespace {
constexpr uint8_t REG_DEVID = 0x00;
constexpr uint8_t REG_BW_RATE = 0x2C;
constexpr uint8_t REG_POWER_CTL = 0x2D;
constexpr uint8_t REG_DATA_FORMAT = 0x31;
constexpr uint8_t REG_DATAX0 = 0x32;
constexpr uint8_t REG_THRESH_ACT = 0x24;
constexpr uint8_t REG_ACT_INACT_CTL = 0x27;
constexpr uint8_t REG_INT_MAP = 0x2F;
constexpr uint8_t REG_INT_ENABLE = 0x2E;
constexpr uint8_t REG_INT_SOURCE = 0x30;
constexpr uint8_t DEVID_ADXL345 = 0xE5;

bool writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(ADXL345_I2C_ADDRESS);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool readRegisters(uint8_t reg, uint8_t* buffer, size_t len) {
    Wire.beginTransmission(ADXL345_I2C_ADDRESS);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        return false;
    }

    size_t received = Wire.requestFrom(static_cast<int>(ADXL345_I2C_ADDRESS), static_cast<int>(len), static_cast<int>(true));
    if (received != len) {
        return false;
    }

    for (size_t i = 0; i < len; ++i) {
        buffer[i] = Wire.read();
    }
    return true;
}

float calculateZeroCrossFrequency(const float* data, uint32_t count, uint16_t sample_rate_hz) {
    if (data == nullptr || count < 3 || sample_rate_hz == 0) {
        return 0.0f;
    }

    // Remove DC bias first to avoid missing crossings on offset signals.
    float mean = 0.0f;
    for (uint32_t i = 0; i < count; ++i) {
        mean += data[i];
    }
    mean /= static_cast<float>(count);

    // Deadband reduces false crossings from small noise around zero.
    const float deadband_g = 0.003f;
    const float dt = 1.0f / static_cast<float>(sample_rate_hz);

    uint32_t crossing_count = 0;
    float first_cross_t = -1.0f;
    float last_cross_t = -1.0f;

    float prev = data[0] - mean;
    for (uint32_t i = 1; i < count; ++i) {
        float curr = data[i] - mean;

        if (fabs(prev) < deadband_g || fabs(curr) < deadband_g) {
            prev = curr;
            continue;
        }

        if ((prev < 0.0f && curr >= 0.0f) || (prev > 0.0f && curr <= 0.0f)) {
            const float denom = (curr - prev);
            float frac = 0.0f;
            if (fabs(denom) > 1e-9f) {
                frac = (-prev) / denom;
            }
            if (frac < 0.0f) frac = 0.0f;
            if (frac > 1.0f) frac = 1.0f;

            const float t_cross = (static_cast<float>(i - 1) + frac) * dt;
            if (first_cross_t < 0.0f) {
                first_cross_t = t_cross;
            }
            last_cross_t = t_cross;
            ++crossing_count;
        }

        prev = curr;
    }

    if (crossing_count < 2 || last_cross_t <= first_cross_t) {
        return 0.0f;
    }

    const float duration_s = last_cross_t - first_cross_t;
    const float crossings_per_second = static_cast<float>(crossing_count - 1) / duration_s;
    // One full cycle contributes two zero-crossings.
    float frequency_hz = 0.5f * crossings_per_second;
    if (!isfinite(frequency_hz) || frequency_hz < 0.0f) {
        return 0.0f;
    }
    return frequency_hz;
}
}

MEMSSensor::MEMSSensor()
    : current_rate_hz(ADXL345_DATA_RATE_HZ),
      current_range_g(ADXL345_RANGE_G),
      interrupt_threshold_mg(250),
      offset_x(ADXL345_OFFSET_X),
      offset_y(ADXL345_OFFSET_Y),
      offset_z(ADXL345_OFFSET_Z),
      spectrum_points(FFT_DISPLAY_POINTS) {
    for (uint16_t i = 0; i < FFT_DISPLAY_POINTS; ++i) {
        spectrum_freq[i] = 10.0f + ((1190.0f * i) / (FFT_DISPLAY_POINTS - 1));
        spectrum_x[i] = 0.0f;
        spectrum_y[i] = 0.0f;
        spectrum_z[i] = 0.0f;
    }
}

MEMSSensor::~MEMSSensor() {
}

bool MEMSSensor::begin() {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, static_cast<uint32_t>(I2C_FREQUENCY));

    uint8_t devid = 0;
    if (!readRegisters(REG_DEVID, &devid, 1) || devid != DEVID_ADXL345) {
        ERROR_PRINT("ADXL345 not found at address 0x%02X", ADXL345_I2C_ADDRESS);
        return false;
    }

    // Put sensor in standby before changing rate/range.
    writeRegister(REG_POWER_CTL, 0x00);
    setDataRate(ADXL345_DATA_RATE_HZ);
    setRange(ADXL345_RANGE_G);
    setOffset(offset_x, offset_y, offset_z);
    setInterruptThreshold(interrupt_threshold_mg);
    // Set MEASURE bit.
    writeRegister(REG_POWER_CTL, 0x08);

    INFO_PRINT("ADXL345 initialized successfully");
    return true;
}

bool MEMSSensor::readRawData(MEMSData& data) {
    data.timestamp_us = micros();
    data.sample_count = MEMS_SAMPLE_COUNT;

    uint8_t raw[6];
    const float scale_g_per_lsb = 0.00390625f;  // Full resolution mode (approx. 3.9 mg/LSB)

    for (uint32_t i = 0; i < MEMS_SAMPLE_COUNT; ++i) {
        if (!readRegisters(REG_DATAX0, raw, sizeof(raw))) {
            return false;
        }

        int16_t x = static_cast<int16_t>(raw[1] << 8 | raw[0]);
        int16_t y = static_cast<int16_t>(raw[3] << 8 | raw[2]);
        int16_t z = static_cast<int16_t>(raw[5] << 8 | raw[4]);

        data.accel_x[i] = (static_cast<float>(x) * scale_g_per_lsb) - offset_x;
        data.accel_y[i] = (static_cast<float>(y) * scale_g_per_lsb) - offset_y;
        data.accel_z[i] = (static_cast<float>(z) * scale_g_per_lsb) - offset_z;

        if (g_debug_mode && i < 3) {
            DEBUG_PRINT("MEMS[%u] X=%.3f Y=%.3f Z=%.3f g", static_cast<unsigned>(i), data.accel_x[i], data.accel_y[i], data.accel_z[i]);
        }

        uint32_t sample_period_us = (current_rate_hz > 0) ? (1000000UL / current_rate_hz) : 625UL;
        delayMicroseconds(sample_period_us);
    }

    return true;
}

bool MEMSSensor::processVibrationData(const MEMSData& raw_data, VibrationAnalysis& analysis) {
    analysis.timestamp_us = raw_data.timestamp_us;
    analysis.rms_accel_x = calculateRMS(raw_data.accel_x, raw_data.sample_count);
    analysis.rms_accel_y = calculateRMS(raw_data.accel_y, raw_data.sample_count);
    analysis.rms_accel_z = calculateRMS(raw_data.accel_z, raw_data.sample_count);

    const float integration_factor = G_TO_MMS2 / (current_rate_hz * 2 * PI);
    analysis.rms_velocity_x = analysis.rms_accel_x * integration_factor;
    analysis.rms_velocity_y = analysis.rms_accel_y * integration_factor;
    analysis.rms_velocity_z = analysis.rms_accel_z * integration_factor;

    performFFT(raw_data.accel_x, raw_data.sample_count, analysis.fft_peak_freq_x, analysis.fft_power_x);
    performFFT(raw_data.accel_y, raw_data.sample_count, analysis.fft_peak_freq_y, analysis.fft_power_y);
    performFFT(raw_data.accel_z, raw_data.sample_count, analysis.fft_peak_freq_z, analysis.fft_power_z);
    buildDisplaySpectrum(raw_data.accel_x, raw_data.sample_count, spectrum_x);
    buildDisplaySpectrum(raw_data.accel_y, raw_data.sample_count, spectrum_y);
    buildDisplaySpectrum(raw_data.accel_z, raw_data.sample_count, spectrum_z);

    // Vibration frequency from raw data using zero-crossing per axis.
    analysis.vibration_freq_x = calculateZeroCrossFrequency(raw_data.accel_x, raw_data.sample_count, current_rate_hz);
    analysis.vibration_freq_y = calculateZeroCrossFrequency(raw_data.accel_y, raw_data.sample_count, current_rate_hz);
    analysis.vibration_freq_z = calculateZeroCrossFrequency(raw_data.accel_z, raw_data.sample_count, current_rate_hz);

    calculatePitchRoll(raw_data, analysis.pitch, analysis.roll);
    analysis.temperature = 0.0f;

    return true;
}

bool MEMSSensor::setDataRate(uint16_t rate_hz) {
    uint8_t rate_code = 0x0E;  // 1600 Hz
    current_rate_hz = 1600;

    if (rate_hz <= 6) {
        rate_code = 0x06; current_rate_hz = 6;
    } else if (rate_hz <= 12) {
        rate_code = 0x07; current_rate_hz = 12;
    } else if (rate_hz <= 25) {
        rate_code = 0x08; current_rate_hz = 25;
    } else if (rate_hz <= 50) {
        rate_code = 0x09; current_rate_hz = 50;
    } else if (rate_hz <= 100) {
        rate_code = 0x0A; current_rate_hz = 100;
    } else if (rate_hz <= 200) {
        rate_code = 0x0B; current_rate_hz = 200;
    } else if (rate_hz <= 400) {
        rate_code = 0x0C; current_rate_hz = 400;
    } else if (rate_hz <= 800) {
        rate_code = 0x0D; current_rate_hz = 800;
    } else if (rate_hz <= 1600) {
        rate_code = 0x0E; current_rate_hz = 1600;
    } else {
        rate_code = 0x0F; current_rate_hz = 3200;
    }

    bool ok = writeRegister(REG_BW_RATE, rate_code);
    DEBUG_PRINT("ADXL345 data rate set to %u Hz", current_rate_hz);
    return ok;
}

bool MEMSSensor::setRange(uint8_t range_g) {
    uint8_t range_code = 0x03;
    current_range_g = 16;

    if (range_g <= 2) {
        range_code = 0x00; current_range_g = 2;
    } else if (range_g <= 4) {
        range_code = 0x01; current_range_g = 4;
    } else if (range_g <= 8) {
        range_code = 0x02; current_range_g = 8;
    } else {
        range_code = 0x03; current_range_g = 16;
    }

    // FULL_RES=1 keeps scale constant across ranges.
    return writeRegister(REG_DATA_FORMAT, static_cast<uint8_t>(0x08 | range_code));
}

bool MEMSSensor::setOffset(float off_x, float off_y, float off_z) {
    offset_x = off_x;
    offset_y = off_y;
    offset_z = off_z;
    return true;
}

bool MEMSSensor::setInterruptThreshold(uint16_t threshold_mg) {
    interrupt_threshold_mg = threshold_mg;
    uint8_t reg_value = static_cast<uint8_t>(threshold_mg / 63);  // ~62.5 mg/LSB
    if (reg_value == 0) {
        reg_value = 1;
    }
    return writeRegister(REG_THRESH_ACT, reg_value);
}

uint16_t MEMSSensor::getDataRate() const {
    return current_rate_hz;
}

uint8_t MEMSSensor::getRange() const {
    return current_range_g;
}

bool MEMSSensor::setupInterrupt(uint8_t int_pin, bool activity) {
    // Enable activity detection on XYZ axes.
    writeRegister(REG_ACT_INACT_CTL, 0x70);
    setInterruptThreshold(interrupt_threshold_mg);

    if (!activity) {
        return writeRegister(REG_INT_ENABLE, 0x00);
    }

    // Route ACTIVITY interrupt to selected pin (INT1/INT2).
    uint8_t int_map = (int_pin == GPIO_ADXL345_INT2) ? 0x10 : 0x00;
    if (!writeRegister(REG_INT_MAP, int_map)) {
        return false;
    }
    return writeRegister(REG_INT_ENABLE, 0x10);
}

bool MEMSSensor::dataAvailable() {
    uint8_t int_source = 0;
    if (!readRegisters(REG_INT_SOURCE, &int_source, 1)) {
        return false;
    }
    return (int_source & 0x80) != 0;
}

bool MEMSSensor::selfTest() {
    uint8_t devid = 0;
    bool ok = readRegisters(REG_DEVID, &devid, 1) && devid == DEVID_ADXL345;
    if (!ok) {
        ERROR_PRINT("ADXL345 self-test failed");
    }
    return ok;
}

float MEMSSensor::calculateRMS(const float* data, uint32_t count) {
    if (count < 2 || current_rate_hz == 0) {
        return 0.0f;
    }

    const float dt = 1.0f / static_cast<float>(current_rate_hz);
    const float rc = 1.0f / (2.0f * PI * RMS_HIGHPASS_CUTOFF_HZ);
    const float alpha = rc / (rc + dt);

    float sum_sq = 0.0f;
    float prev_x = data[0];
    float prev_y = 0.0f;

    for (uint32_t i = 1; i < count; ++i) {
        float y = alpha * (prev_y + data[i] - prev_x);
        sum_sq += y * y;
        prev_x = data[i];
        prev_y = y;
    }

    return sqrt(sum_sq / static_cast<float>(count - 1));
}

void MEMSSensor::calculatePitchRoll(const MEMSData& data, float& pitch, float& roll) {
    float avg_x = 0.0f;
    float avg_y = 0.0f;
    float avg_z = 0.0f;

    for (uint32_t i = 0; i < data.sample_count; ++i) {
        avg_x += data.accel_x[i];
        avg_y += data.accel_y[i];
        avg_z += data.accel_z[i];
    }

    avg_x /= data.sample_count;
    avg_y /= data.sample_count;
    avg_z /= data.sample_count;

    pitch = atan2(avg_x, sqrt(avg_y * avg_y + avg_z * avg_z)) * 180.0f / PI;
    roll = atan2(avg_y, sqrt(avg_x * avg_x + avg_z * avg_z)) * 180.0f / PI;
}

void MEMSSensor::performFFT(const float* acceleration_data, uint32_t count, float& peak_frequency, float& power) {
    if (count < FFT_SIZE) {
        peak_frequency = 0.0f;
        power = 0.0f;
        return;
    }

    static double vReal[FFT_SIZE];
    static double vImag[FFT_SIZE];

    for (uint32_t i = 0; i < FFT_SIZE; ++i) {
        const double window = 0.54 - 0.46 * cos(2.0 * PI * i / (FFT_SIZE - 1));
        vReal[i] = static_cast<double>(acceleration_data[i]) * window;
        vImag[i] = 0.0;
    }

    arduinoFFT FFT = arduinoFFT(vReal, vImag, FFT_SIZE, static_cast<double>(current_rate_hz));
    FFT.Windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFT.Compute(FFT_FORWARD);
    FFT.ComplexToMagnitude();

    double max_magnitude = 0.0;
    uint16_t peak_index = 1;
    for (uint16_t i = 1; i < FFT_SIZE / 2; ++i) {
        if (vReal[i] > max_magnitude) {
            max_magnitude = vReal[i];
            peak_index = i;
        }
    }

    peak_frequency = static_cast<float>((peak_index * current_rate_hz) / static_cast<double>(FFT_SIZE));
    power = static_cast<float>(20.0 * log10(max_magnitude + 1e-12));
}

void MEMSSensor::buildDisplaySpectrum(const float* acceleration_data, uint32_t count, float* spectrum_amplitude_mm_s) {
    if (count < FFT_SIZE || spectrum_amplitude_mm_s == nullptr) {
        for (uint16_t i = 0; i < FFT_DISPLAY_POINTS; ++i) {
            spectrum_amplitude_mm_s[i] = 0.0f;
        }
        return;
    }

    static double vReal[FFT_SIZE];
    static double vImag[FFT_SIZE];

    for (uint32_t i = 0; i < FFT_SIZE; ++i) {
        const double window = 0.54 - 0.46 * cos(2.0 * PI * i / (FFT_SIZE - 1));
        vReal[i] = static_cast<double>(acceleration_data[i]) * window;
        vImag[i] = 0.0;
    }

    arduinoFFT FFT = arduinoFFT(vReal, vImag, FFT_SIZE, static_cast<double>(current_rate_hz));
    FFT.Windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
    FFT.Compute(FFT_FORWARD);
    FFT.ComplexToMagnitude();

    // Hamming coherent gain compensation for amplitude estimation.
    const float window_coherent_gain = 0.54f;

    for (uint16_t i = 0; i < FFT_DISPLAY_POINTS; ++i) {
        const float freq = spectrum_freq[i];
        int32_t bin = static_cast<int32_t>((freq * FFT_SIZE) / current_rate_hz);
        if (bin < 1) {
            bin = 1;
        } else if (bin > (FFT_SIZE / 2 - 1)) {
            bin = FFT_SIZE / 2 - 1;
        }

        // Estimate peak acceleration amplitude in g from FFT bin magnitude.
        float accel_g = static_cast<float>((2.0 * vReal[bin]) / FFT_SIZE);
        accel_g /= window_coherent_gain;

        // Convert acceleration amplitude to velocity amplitude in mm/s.
        const float accel_mm_s2 = accel_g * G_TO_MMS2;
        float velocity_mm_s = 0.0f;
        if (freq > 1.0f) {
            velocity_mm_s = accel_mm_s2 / (2.0f * PI * freq);
        }
        if (velocity_mm_s < 0.0f) {
            velocity_mm_s = 0.0f;
        }
        spectrum_amplitude_mm_s[i] = velocity_mm_s;
    }
}

bool MEMSSensor::getFFTSpectrum(char axis, float* frequency_hz, float* amplitude_mm_s,
                                uint16_t max_points, uint16_t& out_points) const {
    if (frequency_hz == nullptr || amplitude_mm_s == nullptr || max_points == 0) {
        out_points = 0;
        return false;
    }

    uint16_t n = spectrum_points < max_points ? spectrum_points : max_points;
    const float* selected = spectrum_x;
    if (axis == 'y' || axis == 'Y') {
        selected = spectrum_y;
    } else if (axis == 'z' || axis == 'Z') {
        selected = spectrum_z;
    }

    for (uint16_t i = 0; i < n; ++i) {
        frequency_hz[i] = spectrum_freq[i];
        amplitude_mm_s[i] = selected[i];
    }

    out_points = n;
    return true;
}

BatterySensor::BatterySensor() {
}

bool BatterySensor::begin() {
    analogSetAttenuation(ADC_11db);
    return true;
}

float BatterySensor::readVoltage() {
    uint16_t raw_value = analogRead(GPIO_BATTERY_ADC);
    float voltage = (raw_value / 4095.0f) * ADC_REFERENCE_VOLTAGE * VOLTAGE_DIVIDER_RATIO;
    DEBUG_PRINT("Battery ADC: %u -> %.2f V", raw_value, voltage);
    return voltage;
}

float BatterySensor::getBatteryPercentage() {
    float voltage = readVoltage();

    if (voltage >= MAX_BATTERY_V) return 100.0f;
    if (voltage <= MIN_BATTERY_V) return 0.0f;

    return ((voltage - MIN_BATTERY_V) / (MAX_BATTERY_V - MIN_BATTERY_V)) * 100.0f;
}

bool BatterySensor::isBatteryLow() {
    return readVoltage() < BATTERY_LOW_THRESHOLD_V;
}
