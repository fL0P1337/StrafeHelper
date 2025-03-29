// Globals.h
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <map>          // <-- Add
#include <vector>       // <-- Add
#include <atomic>
#include <mutex>        // Or keep windows.h for CRITICAL_SECTION

namespace Globals {

    // --- WinAPI Handles ---
    extern HHOOK g_hHook;
    extern HWND g_hWindow;
    extern HANDLE g_hSpamThread;
    extern HANDLE g_hSpamEvent;
    extern HINSTANCE g_hInstance;

    // --- State Management ---
    struct KeyState {
        std::atomic<bool> physicalKeyDown = false;
        std::atomic<bool> spamming = false;

        KeyState() : physicalKeyDown(false), spamming(false) {}
        KeyState(const KeyState& other) :
            physicalKeyDown(other.physicalKeyDown.load()),
            spamming(other.spamming.load()) {
        }
        KeyState& operator=(const KeyState& other) {
            if (this != &other) {
                physicalKeyDown.store(other.physicalKeyDown.load());
                spamming.store(other.spamming.load());
            }
            return *this;
        }
        KeyState(KeyState&& other) noexcept :
            physicalKeyDown(other.physicalKeyDown.load()),
            spamming(other.spamming.load()) {
        }
        KeyState& operator=(KeyState&& other) noexcept {
            if (this != &other) {
                physicalKeyDown.store(other.physicalKeyDown.load());
                spamming.store(other.spamming.load());
            }
            return *this;
        }
    };

    extern std::map<int, KeyState> g_KeyInfo;
    extern std::vector<int> g_activeSpamKeys;
    extern CRITICAL_SECTION g_csActiveKeys;
    extern std::atomic<bool> g_isCSpamActive;

    // --- Tray Icon ---
    extern NOTIFYICONDATA g_nid;
    // Use #define for constants used in case statements for better compatibility
#define WM_TRAYICON (WM_APP + 1)
#define ID_TRAY_APP_ICON 1001
#define ID_TRAY_EXIT_MENU_ITEM 3000
#define ID_TRAY_TOGGLE_STRAFING_ITEM 3002
// Remove the const UINT versions if you use #define
// const UINT WM_TRAYICON = WM_APP + 1;
// const UINT ID_TRAY_APP_ICON = 1001;
// const UINT ID_TRAY_EXIT_MENU_ITEM = 3000;
// const UINT ID_TRAY_TOGGLE_STRAFING_ITEM = 3002;


} // namespace Globals