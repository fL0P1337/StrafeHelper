// Config.cpp
#include "Config.h"
#include "Utils.h" // For LogError (or move LogError here?)
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <stdexcept>
#include <windows.h> // For VkKeyScanA

namespace Config {
    // --- Definitions of extern variables from Config.h ---
    const char APP_NAME[] = "StrafeHelper";
    const char VERSION[] = "1.2_modular"; // Updated version
    const char CONFIG_FILE_NAME[] = "config.cfg";

    const TCHAR WINDOW_CLASS_NAME[] = TEXT("StrafeHelperWindowClass");
    const TCHAR WINDOW_TITLE[] = TEXT("StrafeHelper Hidden Window");

    // Initialize atomics with defaults
    std::atomic<int> SpamDelayMs = 10;
    std::atomic<int> SpamKeyDownDurationMs = 5;
    std::atomic<bool> IsLocked = false;
    std::atomic<bool> IsWASDStrafingEnabled = true;
    std::atomic<int> KeySpamTrigger = 'C'; // VK_KEY 'C'

    // --- Implementation of LoadConfig ---
    void LoadConfig() {
        std::ifstream configFile(CONFIG_FILE_NAME);
        if (!configFile.is_open()) {
            std::cout << "Config file '" << CONFIG_FILE_NAME << "' not found. Using defaults." << std::endl;
            // Print defaults being used
            std::cout << " Defaults: Delay=" << SpamDelayMs.load()
                << "ms, Duration=" << SpamKeyDownDurationMs.load()
                << "ms, Trigger=" << static_cast<char>(KeySpamTrigger.load())
                << ", Enabled=" << (IsWASDStrafingEnabled.load() ? "true" : "false")
                << std::endl;
            return;
        }

        std::cout << "Loading configuration from " << CONFIG_FILE_NAME << "..." << std::endl;
        std::string line;
        int lineNumber = 0;
        while (std::getline(configFile, line)) {
            lineNumber++;
            std::string trimmedLine;
            size_t first = line.find_first_not_of(" \t\n\r\f\v");
            if (first == std::string::npos || line[first] == '#') {
                continue;
            }
            size_t last = line.find_last_not_of(" \t\n\r\f\v");
            trimmedLine = line.substr(first, (last - first + 1));

            size_t equalsPos = trimmedLine.find('=');
            if (equalsPos == std::string::npos) {
                std::cerr << "Warning: Malformed line " << lineNumber << " in config: " << line << std::endl;
                continue;
            }

            std::string key = trimmedLine.substr(0, equalsPos);
            std::string value = trimmedLine.substr(equalsPos + 1);
            // Trim key/value
            size_t key_last = key.find_last_not_of(" \t\n\r\f\v");
            if (key_last != std::string::npos) key = key.substr(0, key_last + 1);
            size_t value_first = value.find_first_not_of(" \t\n\r\f\v");
            if (value_first != std::string::npos) value = value.substr(value_first);


            try {
                if (key == "SPAM_DELAY_MS")
                    SpamDelayMs = std::stoi(value);
                else if (key == "SPAM_KEY_DOWN_DURATION")
                    SpamKeyDownDurationMs = std::stoi(value);
                else if (key == "isLocked")
                    IsLocked = (value == "true" || value == "1");
                else if (key == "isWASDStrafingEnabled")
                    IsWASDStrafingEnabled = (value == "true" || value == "1");
                else if (key == "KEY_SPAM_TRIGGER") {
                    if (!value.empty()) {
                        SHORT vkScanResult = VkKeyScanA(value[0]);
                        if (vkScanResult != -1) {
                            KeySpamTrigger = LOBYTE(vkScanResult);
                        }
                        else {
                            KeySpamTrigger = static_cast<int>(toupper(value[0]));
                            std::cerr << "Warning: Could not map config key '" << value[0] << "' via VkKeyScanA. Using direct char value." << std::endl;
                        }
                    }
                }
            }
            catch (const std::exception& e) {
                std::cerr << "Warning: Invalid config value on line " << lineNumber << " for key '" << key << "': " << value << " (" << e.what() << ")" << std::endl;
            }
        }
        configFile.close();

        std::cout << " Configuration loaded:" << std::endl;
        std::cout << "  SPAM_DELAY_MS: " << SpamDelayMs.load() << std::endl;
        std::cout << "  SPAM_KEY_DOWN_DURATION: " << SpamKeyDownDurationMs.load() << std::endl;
        std::cout << "  isLocked: " << (IsLocked.load() ? "true" : "false") << std::endl;
        std::cout << "  isWASDStrafingEnabled: " << (IsWASDStrafingEnabled.load() ? "true" : "false") << std::endl;
        std::cout << "  KEY_SPAM_TRIGGER: " << static_cast<char>(KeySpamTrigger.load()) << " (VK: " << KeySpamTrigger.load() << ")" << std::endl;
    }

} // namespace Config