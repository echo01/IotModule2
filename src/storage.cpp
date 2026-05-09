#include "storage.h"

Storage::Storage() : json_error_occurred(false) {
}

bool Storage::begin() {
    if (!SPIFFS.begin(true)) {  // true = format if mount failed
        ERROR_PRINT("SPIFFS mount failed");
        return false;
    }
    
    INFO_PRINT("SPIFFS mounted successfully");
    INFO_PRINT("Total space: %u bytes, Used: %u bytes", 
               getTotalSpace(), getTotalSpace() - getFreeSpace());
    
    return true;
}

bool Storage::loadConfig(SystemConfig& config) {
    if (!fileExists(CONFIG_FILE_PATH)) {
        INFO_PRINT("Config file not found, using defaults");
        config = createDefaultConfig();
        return true;
    }

    File file = SPIFFS.open(CONFIG_FILE_PATH, "r");
    if (!file) {
        ERROR_PRINT("Failed to open config file");

    File file = SPIFFS.open(CONFIG_FILE_PATH, "r");
    if (!file) {
        ERROR_PRINT("Failed to open config file");
        return false;
    }

    StaticJsonDocument<4096> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    StaticJsonDocument<4096> doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    
    if (error) {
        ERROR_PRINT("JSON parse error: %s", error.c_str());
        config = createDefaultConfig();
        return false;
    }
    
    config = json_to_config(doc);
    INFO_PRINT("Configuration loaded from %s", CONFIG_FILE_PATH);
    
    return true;
}

bool Storage::saveConfig(const SystemConfig& config) {
    File file = SPIFFS.open(CONFIG_FILE_PATH, "w");
    if (!file) {
        ERROR_PRINT("Failed to open file for writing: %s", CONFIG_FILE_PATH);
        return false;
    }

    StaticJsonDocument<4096> doc;
    config_to_json(config, doc);
    size_t written = serializeJson(doc, file);
    file.close();

    if (written > 0) {
    File file = SPIFFS.open(CONFIG_FILE_PATH, "w");
    if (!file) {
        ERROR_PRINT("Failed to open file for writing: %s", CONFIG_FILE_PATH);
        return false;
    }

    StaticJsonDocument<4096> doc;
    config_to_json(config, doc);
    size_t written = serializeJson(doc, file);
    file.close();

    if (written > 0) {
        INFO_PRINT("Configuration saved to %s", CONFIG_FILE_PATH);
        return true;
    }
    
    ERROR_PRINT("Failed to save configuration");
    return false;
}

bool Storage::format() {
    if (SPIFFS.format()) {
        INFO_PRINT("SPIFFS formatted successfully");
        return true;
    }
    ERROR_PRINT("SPIFFS format failed");
    return false;
}

uint32_t Storage::getFreeSpace() {
    return SPIFFS.totalBytes() - SPIFFS.usedBytes();
}

uint32_t Storage::getTotalSpace() {
    return SPIFFS.totalBytes();
}

bool Storage::fileExists(const String& path) {
    return SPIFFS.exists(path);
}

bool Storage::deleteFile(const String& path) {
    return SPIFFS.remove(path);
}

bool Storage::factoryReset(bool clear_spiffs) {
    bool ok = true;

    if (fileExists(CONFIG_FILE_PATH)) {
        ok = deleteFile(CONFIG_FILE_PATH) && ok;
    }

    if (clear_spiffs) {
        ok = format() && ok;
    }

    return ok;
}

bool Storage::readFile(const String& path, String& content) {
    File file = SPIFFS.open(path, "r");
    if (!file) {
        ERROR_PRINT("Failed to open file: %s", path.c_str());
        return false;
    }
    
    content = "";
    while (file.available()) {
        content += (char)file.read();
    }
    file.close();
    
    return true;
}

bool Storage::writeFile(const String& path, const String& content) {
    File file = SPIFFS.open(path, "w");
    if (!file) {
        ERROR_PRINT("Failed to open file for writing: %s", path.c_str());
        return false;
    }
    
    size_t written = file.print(content);
    file.close();
    
    return written == content.length();
}

SystemConfig Storage::createDefaultConfig() {
    SystemConfig config = {};
    
    // WiFi defaults
    strlcpy(config.wifi_ssid, "", sizeof(config.wifi_ssid));
    strlcpy(config.wifi_password, "", sizeof(config.wifi_password));
    config.wifi_ap_enabled = true;
    strlcpy(config.ap_ssid, "", sizeof(config.ap_ssid));
    strlcpy(config.ap_password, "12345678", sizeof(config.ap_password));
    config.sta_use_static_ip = false;
    strlcpy(config.sta_static_ip, "", sizeof(config.sta_static_ip));
    strlcpy(config.sta_gateway, "", sizeof(config.sta_gateway));
    strlcpy(config.sta_subnet, "", sizeof(config.sta_subnet));
    strlcpy(config.sta_dns1, "", sizeof(config.sta_dns1));
    strlcpy(config.sta_dns2, "", sizeof(config.sta_dns2));
    
    // MQTT defaults
    strlcpy(config.mqtt_broker, "broker.hivemq.com", sizeof(config.mqtt_broker));
    config.mqtt_port = 1883;
    strlcpy(config.mqtt_client_id, "ESP32_VIOT", sizeof(config.mqtt_client_id));
    strlcpy(config.mqtt_username, "", sizeof(config.mqtt_username));
    strlcpy(config.mqtt_password, "", sizeof(config.mqtt_password));
    strlcpy(config.mqtt_topic_publish, "viot/vibration", sizeof(config.mqtt_topic_publish));
    strlcpy(config.mqtt_topic_fft_x, "viot/vibration/fft/x", sizeof(config.mqtt_topic_fft_x));
    strlcpy(config.mqtt_topic_fft_y, "viot/vibration/fft/y", sizeof(config.mqtt_topic_fft_y));
    strlcpy(config.mqtt_topic_fft_z, "viot/vibration/fft/z", sizeof(config.mqtt_topic_fft_z));
    strlcpy(config.mqtt_topic_subscribe, "viot/config", sizeof(config.mqtt_topic_subscribe));
    config.mqtt_publish_interval_s = 60;
    config.mqtt_use_tls = false;
    config.mqtt_aws_iot_enabled = false;
    
    // ADXL345 defaults
    config.adxl345_rate_hz = 1600;
    config.adxl345_range_g = 16;
    config.adxl345_offset_x = 0.0f;
    config.adxl345_offset_y = 0.0f;
    config.adxl345_offset_z = 0.0f;
    config.adxl345_int_threshold_mg = 250;
    config.adxl345_int_enabled = true;
    config.adxl345_int_pin = 1;
    
    // Vibration signal validation defaults
    config.vibration_min_rms_g = VIBRATION_MIN_RMS_G;
    config.vibration_min_peak_g = VIBRATION_MIN_PEAK_G;
    config.vibration_noise_floor_db = VIBRATION_NOISE_FLOOR_DB;
    config.vibration_deadband_g = VIBRATION_DEADBAND_G;
    config.vibration_min_freq_hz = VIBRATION_MIN_FREQUENCY_HZ;
    config.vibration_max_freq_hz = VIBRATION_MAX_FREQUENCY_HZ;
    
    // Power defaults
    config.sleep_enabled = true;
    config.sleep_interval_sec = DEEP_SLEEP_INTERVAL_SEC;
    config.log_enabled = true;
    
    return config;
}

void Storage::config_to_json(const SystemConfig& config, JsonDocument& doc) {
void Storage::config_to_json(const SystemConfig& config, JsonDocument& doc) {
    doc["wifi"]["ssid"] = config.wifi_ssid;
    doc["wifi"]["password"] = config.wifi_password;
    doc["wifi"]["ap_enabled"] = config.wifi_ap_enabled;
    doc["wifi"]["ap_ssid"] = config.ap_ssid;
    doc["wifi"]["ap_password"] = config.ap_password;
    doc["wifi"]["sta_use_static_ip"] = config.sta_use_static_ip;
    doc["wifi"]["sta_static_ip"] = config.sta_static_ip;
    doc["wifi"]["sta_gateway"] = config.sta_gateway;
    doc["wifi"]["sta_subnet"] = config.sta_subnet;
    doc["wifi"]["sta_dns1"] = config.sta_dns1;
    doc["wifi"]["sta_dns2"] = config.sta_dns2;
    
    doc["mqtt"]["broker"] = config.mqtt_broker;
    doc["mqtt"]["port"] = config.mqtt_port;
    doc["mqtt"]["client_id"] = config.mqtt_client_id;
    doc["mqtt"]["username"] = config.mqtt_username;
    doc["mqtt"]["password"] = config.mqtt_password;
    doc["mqtt"]["topic_publish"] = config.mqtt_topic_publish;
    doc["mqtt"]["topic_fft_x"] = config.mqtt_topic_fft_x;
    doc["mqtt"]["topic_fft_y"] = config.mqtt_topic_fft_y;
    doc["mqtt"]["topic_fft_z"] = config.mqtt_topic_fft_z;
    doc["mqtt"]["topic_subscribe"] = config.mqtt_topic_subscribe;
    doc["mqtt"]["publish_interval_sec"] = config.mqtt_publish_interval_s;
    doc["mqtt"]["use_tls"] = config.mqtt_use_tls;
    doc["mqtt"]["aws_iot_enabled"] = config.mqtt_aws_iot_enabled;
    
    doc["adxl345"]["rate_hz"] = config.adxl345_rate_hz;
    doc["adxl345"]["range_g"] = config.adxl345_range_g;
    doc["adxl345"]["offset_x"] = config.adxl345_offset_x;
    doc["adxl345"]["offset_y"] = config.adxl345_offset_y;
    doc["adxl345"]["offset_z"] = config.adxl345_offset_z;
    doc["adxl345"]["int_threshold_mg"] = config.adxl345_int_threshold_mg;
    doc["adxl345"]["int_enabled"] = config.adxl345_int_enabled;
    doc["adxl345"]["int_pin"] = config.adxl345_int_pin;
    
    doc["vibration"]["min_rms_g"] = config.vibration_min_rms_g;
    doc["vibration"]["min_peak_g"] = config.vibration_min_peak_g;
    doc["vibration"]["noise_floor_db"] = config.vibration_noise_floor_db;
    doc["vibration"]["deadband_g"] = config.vibration_deadband_g;
    doc["vibration"]["min_freq_hz"] = config.vibration_min_freq_hz;
    doc["vibration"]["max_freq_hz"] = config.vibration_max_freq_hz;
    
    doc["power"]["sleep_enabled"] = config.sleep_enabled;
    doc["power"]["sleep_interval_sec"] = config.sleep_interval_sec;
    doc["power"]["log_enabled"] = config.log_enabled;
}

SystemConfig Storage::json_to_config(const JsonDocument& doc) {
    SystemConfig config = createDefaultConfig();
    
    if (doc["wifi"]["ssid"]) 
        strlcpy(config.wifi_ssid, doc["wifi"]["ssid"], sizeof(config.wifi_ssid));
    if (doc["wifi"]["password"]) 
        strlcpy(config.wifi_password, doc["wifi"]["password"], sizeof(config.wifi_password));
    config.wifi_ap_enabled = doc["wifi"]["ap_enabled"] | true;
    if (doc["wifi"]["ap_ssid"])
        strlcpy(config.ap_ssid, doc["wifi"]["ap_ssid"], sizeof(config.ap_ssid));
    if (doc["wifi"]["ap_password"])
        strlcpy(config.ap_password, doc["wifi"]["ap_password"], sizeof(config.ap_password));
    config.sta_use_static_ip = doc["wifi"]["sta_use_static_ip"] | false;
    if (doc["wifi"]["sta_static_ip"])
        strlcpy(config.sta_static_ip, doc["wifi"]["sta_static_ip"], sizeof(config.sta_static_ip));
    if (doc["wifi"]["sta_gateway"])
        strlcpy(config.sta_gateway, doc["wifi"]["sta_gateway"], sizeof(config.sta_gateway));
    if (doc["wifi"]["sta_subnet"])
        strlcpy(config.sta_subnet, doc["wifi"]["sta_subnet"], sizeof(config.sta_subnet));
    if (doc["wifi"]["sta_dns1"])
        strlcpy(config.sta_dns1, doc["wifi"]["sta_dns1"], sizeof(config.sta_dns1));
    if (doc["wifi"]["sta_dns2"])
        strlcpy(config.sta_dns2, doc["wifi"]["sta_dns2"], sizeof(config.sta_dns2));
    
    if (doc["mqtt"]["broker"]) 
        strlcpy(config.mqtt_broker, doc["mqtt"]["broker"], sizeof(config.mqtt_broker));
    config.mqtt_port = doc["mqtt"]["port"] | 1883;
    if (doc["mqtt"]["client_id"]) 
        strlcpy(config.mqtt_client_id, doc["mqtt"]["client_id"], sizeof(config.mqtt_client_id));
    if (doc["mqtt"]["username"])
        strlcpy(config.mqtt_username, doc["mqtt"]["username"], sizeof(config.mqtt_username));
    if (doc["mqtt"]["password"])
        strlcpy(config.mqtt_password, doc["mqtt"]["password"], sizeof(config.mqtt_password));
    if (doc["mqtt"]["topic_publish"]) 
        strlcpy(config.mqtt_topic_publish, doc["mqtt"]["topic_publish"], 
                sizeof(config.mqtt_topic_publish));
    if (doc["mqtt"]["topic_fft_x"]) {
        strlcpy(config.mqtt_topic_fft_x, doc["mqtt"]["topic_fft_x"], sizeof(config.mqtt_topic_fft_x));
    } else {
        strlcpy(config.mqtt_topic_fft_x, "viot/vibration/fft/x", sizeof(config.mqtt_topic_fft_x));
    }
    if (doc["mqtt"]["topic_fft_y"]) {
        strlcpy(config.mqtt_topic_fft_y, doc["mqtt"]["topic_fft_y"], sizeof(config.mqtt_topic_fft_y));
    } else {
        strlcpy(config.mqtt_topic_fft_y, "viot/vibration/fft/y", sizeof(config.mqtt_topic_fft_y));
    }
    if (doc["mqtt"]["topic_fft_z"]) {
        strlcpy(config.mqtt_topic_fft_z, doc["mqtt"]["topic_fft_z"], sizeof(config.mqtt_topic_fft_z));
    } else {
        strlcpy(config.mqtt_topic_fft_z, "viot/vibration/fft/z", sizeof(config.mqtt_topic_fft_z));
    }
    if (doc["mqtt"]["topic_subscribe"])
        strlcpy(config.mqtt_topic_subscribe, doc["mqtt"]["topic_subscribe"],
                sizeof(config.mqtt_topic_subscribe));

    config.mqtt_publish_interval_s = doc["mqtt"]["publish_interval_sec"] | 60;
    config.mqtt_use_tls = doc["mqtt"]["use_tls"] | false;
    config.mqtt_aws_iot_enabled = doc["mqtt"]["aws_iot_enabled"] | false;
    
    config.adxl345_rate_hz = doc["adxl345"]["rate_hz"] | 1600;
    if (config.adxl345_rate_hz <= 400) {
        config.adxl345_rate_hz = 400;
    } else if (config.adxl345_rate_hz <= 800) {
        config.adxl345_rate_hz = 800;
    } else {
        config.adxl345_rate_hz = 1600;
    }
    config.adxl345_range_g = doc["adxl345"]["range_g"] | 16;
    config.adxl345_offset_x = doc["adxl345"]["offset_x"] | 0.0f;
    config.adxl345_offset_y = doc["adxl345"]["offset_y"] | 0.0f;
    config.adxl345_offset_z = doc["adxl345"]["offset_z"] | 0.0f;
    config.adxl345_int_threshold_mg = doc["adxl345"]["int_threshold_mg"] | 250;
    config.adxl345_int_enabled = doc["adxl345"]["int_enabled"] | true;
    config.adxl345_int_pin = doc["adxl345"]["int_pin"] | 1;
    if (config.adxl345_int_pin != 1 && config.adxl345_int_pin != 2) {
        config.adxl345_int_pin = 1;
    }
    
    config.vibration_min_rms_g = doc["vibration"]["min_rms_g"] | VIBRATION_MIN_RMS_G;
    config.vibration_min_peak_g = doc["vibration"]["min_peak_g"] | VIBRATION_MIN_PEAK_G;
    config.vibration_noise_floor_db = doc["vibration"]["noise_floor_db"] | VIBRATION_NOISE_FLOOR_DB;
    config.vibration_deadband_g = doc["vibration"]["deadband_g"] | VIBRATION_DEADBAND_G;
    config.vibration_min_freq_hz = doc["vibration"]["min_freq_hz"] | VIBRATION_MIN_FREQUENCY_HZ;
    config.vibration_max_freq_hz = doc["vibration"]["max_freq_hz"] | VIBRATION_MAX_FREQUENCY_HZ;
    
    config.sleep_enabled = doc["power"]["sleep_enabled"] | true;
    config.sleep_interval_sec = doc["power"]["sleep_interval_sec"] | DEEP_SLEEP_INTERVAL_SEC;
    config.log_enabled = doc["power"]["log_enabled"] | true;
    
    return config;
}
