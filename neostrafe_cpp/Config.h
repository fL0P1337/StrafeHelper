// Config.h
#pragma once

#include <atomic>
#include <string>
#include <tchar.h> // For TCHAR

namespace Config {
    // Application Info
    extern const char APP_NAME[];
    extern const char VERSION[];
    extern const char CONFIG_FILE_NAME[];

    // Window Info
    extern const TCHAR WINDOW_CLASS_NAME[];
    extern const TCHAR WINDOW_TITLE[];

    // Default Settings / Current Values (Declared extern)
    extern std::atomic<int> SpamDelayMs;
    extern std::atomic<int> SpamKeyDownDurationMs;
    extern std::atomic<bool> IsLocked; // Feature toggle
    extern std::atomic<bool> IsWASDStrafingEnabled; // Master switch
    extern std::atomic<bool> EnableSnapTap; // SnapTap / SOCD filtering
    extern std::atomic<int> KeySpamTrigger; // VK Code

    // Function to load configuration
    void LoadConfig();
    void SaveConfig();

} // namespace Config
