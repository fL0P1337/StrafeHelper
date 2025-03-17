#pragma once
#include <string>
#include <atomic>
#include <iostream>
#include <fstream>
#include <ctime>
#include <windows.h>

class Config {
private:
    static Config* instance;
    std::string configPath = "config.cfg";
    std::string lastSaveTime;
    std::string currentUser;

    // Private constructor for singleton
    Config() {
        currentUser = "fL0P1337";
        updateLastSaveTime();

        // Initialize atomic values
        spamDelayMs.store(1);
        spamKeyDownDuration.store(1);
        isWASDStrafingEnabled.store(true);
        spamTriggerKey.store('C');
        showConsole.store(true);
        startWithWindows.store(false);
        minimizeToTray.store(true);
    }

public:
    // Configuration variables with atomic storage
    std::atomic<int> spamDelayMs;
    std::atomic<int> spamKeyDownDuration;
    std::atomic<bool> isWASDStrafingEnabled;
    std::atomic<int> spamTriggerKey;
    std::atomic<bool> showConsole;
    std::atomic<bool> startWithWindows;
    std::atomic<bool> minimizeToTray;

    static Config* getInstance() {
        if (instance == nullptr) {
            instance = new Config();
        }
        return instance;
    }

    void updateLastSaveTime() {
        time_t now = time(0);
        tm utc_tm;
        gmtime_s(&utc_tm, &now);
        char buffer[32];
        strftime(buffer, 32, "%Y-%m-%d %H:%M:%S", &utc_tm);
        lastSaveTime = buffer;
    }

    bool load() {
        std::ifstream file(configPath);
        if (!file.is_open()) {
            std::cout << "Creating new configuration file...\n";
            save(); // Create default config
            return false;
        }

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;

            size_t pos = line.find('=');
            if (pos == std::string::npos) continue;

            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);

            // Remove whitespace
            key.erase(remove_if(key.begin(), key.end(), isspace), key.end());
            value.erase(remove_if(value.begin(), value.end(), isspace), value.end());

            try {
                if (key == "SPAM_DELAY_MS")
                    spamDelayMs.store(std::stoi(value));
                else if (key == "SPAM_KEY_DOWN_DURATION")
                    spamKeyDownDuration.store(std::stoi(value));
                else if (key == "WASD_STRAFING_ENABLED")
                    isWASDStrafingEnabled.store(value == "true" || value == "1");
                else if (key == "SPAM_TRIGGER_KEY")
                    spamTriggerKey.store(value[0]);
                else if (key == "SHOW_CONSOLE")
                    showConsole.store(value == "true" || value == "1");
                else if (key == "START_WITH_WINDOWS")
                    startWithWindows.store(value == "true" || value == "1");
                else if (key == "MINIMIZE_TO_TRAY")
                    minimizeToTray.store(value == "true" || value == "1");
            }
            catch (const std::exception& e) {
                std::cout << "Error parsing config value: " << key << "=" << value << "\n";
            }
        }
        file.close();
        return true;
    }

    bool save() {
        updateLastSaveTime();
        std::ofstream file(configPath);
        if (!file.is_open()) {
            std::cout << "Failed to save configuration!\n";
            return false;
        }

        file << "# StrafeHelper Configuration\n";
        file << "# Last updated: " << lastSaveTime << " UTC\n";
        file << "# User: " << currentUser << "\n\n";

        file << "# Performance Settings\n";
        file << "SPAM_DELAY_MS=" << spamDelayMs.load() << "\n";
        file << "SPAM_KEY_DOWN_DURATION=" << spamKeyDownDuration.load() << "\n\n";

        file << "# Feature Settings\n";
        file << "WASD_STRAFING_ENABLED=" << (isWASDStrafingEnabled.load() ? "true" : "false") << "\n";
        file << "SPAM_TRIGGER_KEY=" << static_cast<char>(spamTriggerKey.load()) << "\n\n";

        file << "# Application Settings\n";
        file << "SHOW_CONSOLE=" << (showConsole.load() ? "true" : "false") << "\n";
        file << "START_WITH_WINDOWS=" << (startWithWindows.load() ? "true" : "false") << "\n";
        file << "MINIMIZE_TO_TRAY=" << (minimizeToTray.load() ? "true" : "false") << "\n";

        file.close();
        return true;
    }

    void setStartWithWindows(bool enable) {
        HKEY hKey;
        LPCTSTR keyPath = TEXT("Software\\Microsoft\\Windows\\CurrentVersion\\Run");

        if (RegOpenKeyEx(HKEY_CURRENT_USER, keyPath, 0, KEY_ALL_ACCESS, &hKey) == ERROR_SUCCESS) {
            if (enable) {
                char path[MAX_PATH];
                GetModuleFileNameA(NULL, path, MAX_PATH);
                RegSetValueExA(hKey, "StrafeHelper", 0, REG_SZ, (BYTE*)path, strlen(path) + 1);
            }
            else {
                RegDeleteValueA(hKey, "StrafeHelper");
            }
            RegCloseKey(hKey);
        }
        startWithWindows.store(enable);
        save();
    }
};

// Initialize the singleton instance
Config* Config::instance = nullptr;