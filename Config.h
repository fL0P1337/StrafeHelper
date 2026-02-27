// Config.h
#pragma once

#include <atomic>
#include <string>
#include <tchar.h> // For TCHAR

namespace Config {

// --- Keybind Mode System ---
enum class KeybindMode : int {
  Hold = 0,   // Active only while key is pressed
  Toggle = 1, // Press to toggle active state
};

// --- Input Backend Selection ---
enum class InputBackendKind : int {
  KbdHook = 0,      // WH_KEYBOARD_LL hook (default, no extra driver needed)
  Interception = 1, // Interception kernel driver (requires interception.dll)
};

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
extern std::atomic<bool> IsLocked;      // Feature toggle
extern std::atomic<bool> EnableSpam;    // Master spam switch
extern std::atomic<bool> EnableSnapTap; // SnapTap / SOCD filtering
extern std::atomic<int> KeySpamTrigger; // VK Code
extern std::atomic<KeybindMode> KeySpamTriggerMode; // Hold or Toggle
extern std::atomic<bool> KeySpamTriggerToggleActive; // Toggle state

// Input backend (persisted as integer 0=KbdHook, 1=Interception)
extern std::atomic<int> SelectedBackend;

// Turbo Loot
extern std::atomic<bool> EnableTurboLoot;
extern std::atomic<int> TurboLootKey; // VK code (default 0x45 = 'E')
extern std::atomic<int> TurboLootDelayMs;
extern std::atomic<int> TurboLootDurationMs;
extern std::atomic<KeybindMode> TurboLootMode;
extern std::atomic<bool> TurboLootToggleActive;

// Turbo Jump
extern std::atomic<bool> EnableTurboJump;
extern std::atomic<int> TurboJumpKey; // VK code (default 0x20 = VK_SPACE)
extern std::atomic<int> TurboJumpDelayMs;
extern std::atomic<int> TurboJumpDurationMs;
extern std::atomic<KeybindMode> TurboJumpMode;
extern std::atomic<bool> TurboJumpToggleActive;

// Superglide
extern std::atomic<bool> EnableSuperglide;
extern std::atomic<int> SuperglideBind; // VK code (default 0xC0 = tilde ~)
extern std::atomic<double> TargetFPS;
extern std::atomic<KeybindMode> SuperglideMode;
extern std::atomic<bool> SuperglideToggleActive;

// Function to load configuration
void LoadConfig();
void SaveConfig();

} // namespace Config
