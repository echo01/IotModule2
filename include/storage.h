#ifndef STORAGE_H
#define STORAGE_H

#include <ArduinoJson.h>
#include <SPIFFS.h>
#include "config.h"
#include "common.h"

class Storage {
public:
    Storage();
    
    // Initialize filesystem
    bool begin();
    
    // Load configuration from JSON file
    bool loadConfig(SystemConfig& config);
    
    // Save configuration to JSON file
    bool saveConfig(const SystemConfig& config);
    
    // Format filesystem
    bool format();
    
    // Get free space
    uint32_t getFreeSpace();
    
    // Get total space
    uint32_t getTotalSpace();
    
    // Check if file exists
    bool fileExists(const String& path);
    
    // Delete file
    bool deleteFile(const String& path);

    // Reset config file and optionally erase filesystem contents
    bool factoryReset(bool clear_spiffs);
    
    // Read raw file
    bool readFile(const String& path, String& content);
    
    // Write raw file
    bool writeFile(const String& path, const String& content);
    
    // Create default config
    static SystemConfig createDefaultConfig();

private:
    bool json_error_occurred;
    
    // Helper for JSON serialization
    void config_to_json(const SystemConfig& config, JsonDocument& doc);
    SystemConfig json_to_config(const JsonDocument& doc);
};

#endif // STORAGE_H
