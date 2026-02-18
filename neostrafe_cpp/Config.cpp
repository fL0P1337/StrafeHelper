// Config.cpp
#include "Config.h"
#include "Utils.h" // For LogError (or move LogError here?)
#include <fstream>
#include <sstream>
#include <iostream>
#include <string>
#include <vector>
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
    std::atomic<bool> EnableSnapTap = true;
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
                << ", SnapTap=" << (EnableSnapTap.load() ? "true" : "false")
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
                else if (key == "enable_snaptap")
                    EnableSnapTap = (value == "true" || value == "1");
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
        std::cout << "  enable_snaptap: " << (EnableSnapTap.load() ? "true" : "false") << std::endl;
        std::cout << "  KEY_SPAM_TRIGGER: " << static_cast<char>(KeySpamTrigger.load()) << " (VK: " << KeySpamTrigger.load() << ")" << std::endl;
    }

    void SaveConfig() {
        const std::string spamDelay = std::to_string(SpamDelayMs.load());
        const std::string spamDuration = std::to_string(SpamKeyDownDurationMs.load());
        const std::string isLockedStr = IsLocked.load() ? "true" : "false";
        const std::string isStrafingStr = IsWASDStrafingEnabled.load() ? "true" : "false";
        const std::string snapTapStr = EnableSnapTap.load() ? "true" : "false";
        const std::string triggerStr(1, static_cast<char>(KeySpamTrigger.load()));

        // Update-in-place: preserve unknown keys/comments, replace known keys, append missing ones.
        std::vector<std::string> lines;
        {
            std::ifstream in(CONFIG_FILE_NAME);
            std::string line;
            while (std::getline(in, line)) {
                lines.push_back(line);
            }
        }

        struct Entry { const char* key; std::string value; bool found; };
        Entry entries[] = {
            { "SPAM_DELAY_MS", spamDelay, false },
            { "SPAM_KEY_DOWN_DURATION", spamDuration, false },
            { "isLocked", isLockedStr, false },
            { "isWASDStrafingEnabled", isStrafingStr, false },
            { "enable_snaptap", snapTapStr, false },
            { "KEY_SPAM_TRIGGER", triggerStr, false },
        };

        auto trim = [](std::string& s) {
            size_t first = s.find_first_not_of(" \t\n\r\f\v");
            if (first == std::string::npos) { s.clear(); return; }
            size_t last = s.find_last_not_of(" \t\n\r\f\v");
            s = s.substr(first, last - first + 1);
        };

        for (std::string& line : lines) {
            std::string working = line;
            trim(working);
            if (working.empty() || working[0] == '#') {
                continue;
            }

            size_t eq = working.find('=');
            if (eq == std::string::npos) {
                continue;
            }

            std::string key = working.substr(0, eq);
            trim(key);

            for (auto& e : entries) {
                if (key == e.key) {
                    line = key + " = " + e.value;
                    e.found = true;
                    break;
                }
            }
        }

        bool anyMissing = false;
        for (const auto& e : entries) {
            if (!e.found) {
                anyMissing = true;
                break;
            }
        }

        if (lines.empty()) {
            lines.push_back("# Configuration file for StrafeHelper");
        }

        if (anyMissing) {
            lines.push_back("");
            lines.push_back("# Updated by StrafeHelper");
            for (const auto& e : entries) {
                if (!e.found) {
                    lines.push_back(std::string(e.key) + " = " + e.value);
                }
            }
        }

        std::ofstream out(CONFIG_FILE_NAME, std::ios::trunc);
        if (!out.is_open()) {
            std::cerr << "Warning: Failed to open config file '" << CONFIG_FILE_NAME << "' for writing." << std::endl;
            return;
        }

        for (size_t i = 0; i < lines.size(); ++i) {
            out << lines[i];
            if (i + 1 < lines.size()) out << std::endl;
        }
        out.close();

        std::cout << "Configuration saved to " << CONFIG_FILE_NAME << std::endl;
    }

} // namespace Config
