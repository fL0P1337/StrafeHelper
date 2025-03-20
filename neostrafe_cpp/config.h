#pragma once
#include <string>
#include <atomic>
#include <iostream>
#include <fstream>
#include <ctime>
#include <windows.h>
#include <iomanip>
#include <sstream>

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
        // Removed: startWithWindows
        minimizeToTray.store(true);
        isSuperGlideEnabled.store(false);
        targetFPS.store(144);
        superGlideTriggerKey.store(VK_SPACE);
        wasdStrafingTriggerKey.store('C');
        superGlideJumpDelay.store(0.0);
        superGlideCrouchDelay.store(0.00094);
    }

public:
    // Configuration variables with atomic storage
    std::atomic<int> spamDelayMs;
    std::atomic<int> spamKeyDownDuration;
    std::atomic<bool> isWASDStrafingEnabled;
    std::atomic<int> spamTriggerKey;
    std::atomic<bool> showConsole;
    // Removed: startWithWindows
    std::atomic<bool> minimizeToTray;
    std::atomic<bool> isSuperGlideEnabled;
    std::atomic<int> targetFPS;
    std::atomic<int> superGlideTriggerKey;
    std::atomic<int> wasdStrafingTriggerKey;
    std::atomic<double> superGlideJumpDelay;
    std::atomic<double> superGlideCrouchDelay;

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
            if (showConsole.load()) {
                std::cout << "Creating new configuration file...\n";
            }
            save();
            return false;
        }

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            size_t pos = line.find('=');
            if (pos == std::string::npos) continue;
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            key.erase(remove_if(key.begin(), key.end(), isspace), key.end());
            value.erase(remove_if(value.begin(), value.end(), isspace), value.end());
            try {
                if (key == "SPAM_DELAY_MS")
                    spamDelayMs.store(std::stoi(value));
                else if (key == "SPAM_KEY_DOWN_DURATION")
                    spamKeyDownDuration.store(std::stoi(value));
                else if (key == "WASD_STRAFING_ENABLED")
                    isWASDStrafingEnabled.store(value == "true" || value == "1");
                else if (key == "WASD_STRAFING_TRIGGER_KEY") {
                    if (value.length() == 1)
                        wasdStrafingTriggerKey.store(value[0]);
                    else
                        wasdStrafingTriggerKey.store(std::stoi(value));
                }
                else if (key == "SUPERGLIDE_TRIGGER_KEY") {
                    if (value.length() == 1)
                        superGlideTriggerKey.store(value[0]);
                    else
                        superGlideTriggerKey.store(std::stoi(value));
                }
                else if (key == "SHOW_CONSOLE")
                    showConsole.store(value == "true" || value == "1");
                else if (key == "MINIMIZE_TO_TRAY")
                    minimizeToTray.store(value == "true" || value == "1");
                else if (key == "SUPERGLIDE_ENABLED")
                    isSuperGlideEnabled.store(value == "true" || value == "1");
                else if (key == "TARGET_FPS")
                    targetFPS.store(std::stoi(value));
                else if (key == "SUPERGLIDE_JUMP_DELAY")
                    superGlideJumpDelay.store(std::stod(value));
                else if (key == "SUPERGLIDE_CROUCH_DELAY")
                    superGlideCrouchDelay.store(std::stod(value));
                // Removed: START_WITH_WINDOWS key
            }
            catch (const std::exception& e) {
                if (showConsole.load()) {
                    std::cout << "Error parsing config value: " << key << "=" << value << "\n";
                }
            }
        }
        file.close();
        return true;
    }

    bool save() {
        updateLastSaveTime();
        std::ofstream file(configPath);
        if (!file.is_open()) {
            if (showConsole.load()) {
                std::cout << "Failed to save configuration!\n";
            }
            return false;
        }
        std::stringstream ss;
        file << "# StrafeHelper Configuration\n";
        file << "# Last updated: " << lastSaveTime << " UTC\n";
        file << "# User: " << currentUser << "\n\n";
        file << "# Performance Settings\n";
        file << "SPAM_DELAY_MS=" << spamDelayMs.load() << "\n";
        file << "SPAM_KEY_DOWN_DURATION=" << spamKeyDownDuration.load() << "\n\n";
        file << "# Feature Settings\n";
        file << "WASD_STRAFING_ENABLED=" << (isWASDStrafingEnabled.load() ? "true" : "false") << "\n";
        file << "WASD_STRAFING_TRIGGER_KEY=" << wasdStrafingTriggerKey.load() << "\n";
        file << "SUPERGLIDE_TRIGGER_KEY=" << superGlideTriggerKey.load() << "\n\n";
        file << "# SuperGlide Settings\n";
        file << "SUPERGLIDE_ENABLED=" << (isSuperGlideEnabled.load() ? "true" : "false") << "\n";
        file << "TARGET_FPS=" << targetFPS.load() << "\n";
        ss << std::fixed << std::setprecision(5);
        ss << superGlideJumpDelay.load();
        file << "SUPERGLIDE_JUMP_DELAY=" << ss.str() << "\n";
        ss.str("");
        ss << std::fixed << std::setprecision(5);
        ss << superGlideCrouchDelay.load();
        file << "SUPERGLIDE_CROUCH_DELAY=" << ss.str() << "\n\n";
        file << "# Application Settings\n";
        file << "SHOW_CONSOLE=" << (showConsole.load() ? "true" : "false") << "\n";
        file << "MINIMIZE_TO_TRAY=" << (minimizeToTray.load() ? "true" : "false") << "\n";
        // Removed: START_WITH_WINDOWS entry
        file.close();
        return true;
    }

    // Removed: setStartWithWindows function

    double getFrameTime() const {
        return 1000.0 / targetFPS.load();
    }
};

Config* Config::instance = nullptr;
