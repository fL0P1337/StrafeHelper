// TrayIcon.cpp
#include "TrayIcon.h"
#include "Globals.h"      // <-- Add
#include "Config.h"
#include "SpamLogic.h"
#include "KeyboardHook.h"
#include "Utils.h"
#include <strsafe.h>
#include <iostream>
#include <tchar.h>
#include <windows.h>      // <-- Add (for Shell_NotifyIcon, LoadImage etc)
#include <shellapi.h>     // <-- Add (Specific for Shell_NotifyIcon maybe)


void InitNotifyIconData(HWND hwnd) {
    // Use Globals:: prefix
    ZeroMemory(&Globals::g_nid, sizeof(Globals::g_nid));
    Globals::g_nid.cbSize = sizeof(NOTIFYICONDATA);
    Globals::g_nid.hWnd = hwnd;
    Globals::g_nid.uID = ID_TRAY_APP_ICON; // Use #define or Globals::ID_TRAY_APP_ICON
    Globals::g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    Globals::g_nid.uCallbackMessage = WM_TRAYICON; // Use #define or Globals::WM_TRAYICON

    HINSTANCE hInstance = Globals::g_hInstance; // Use Globals::
    Globals::g_nid.hIcon = (HICON)LoadImage(hInstance, IDI_APPLICATION, IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    if (!Globals::g_nid.hIcon) {
        Globals::g_nid.hIcon = LoadIcon(NULL, IDI_WARNING);
    }

    // StringCchPrintf uses ARRAYSIZE internally - should be fine if windows.h/strsafe.h included
    StringCchPrintf(Globals::g_nid.szTip, ARRAYSIZE(Globals::g_nid.szTip), TEXT("%hs v%hs"),
        Config::APP_NAME, Config::VERSION);

    if (!Shell_NotifyIcon(NIM_ADD, &Globals::g_nid)) { // Globals::
        LogError("Shell_NotifyIcon(NIM_ADD) failed");
    }
    else {
        Globals::g_nid.uVersion = NOTIFYICON_VERSION_4; // Globals::
        if (!Shell_NotifyIcon(NIM_SETVERSION, &Globals::g_nid)) { // Globals::
            std::cerr << "Warning: Failed to set tray icon version 4." << std::endl;
        }
        std::cout << "Tray icon added." << std::endl;
    }
}

void ShowContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    UINT checkFlag = Config::EnableSpam.load(std::memory_order_relaxed) ? MF_CHECKED : MF_UNCHECKED;
    UINT checkSnapTap = Config::EnableSnapTap.load(std::memory_order_relaxed) ? MF_CHECKED : MF_UNCHECKED;
    // Use #define or Globals:: prefix for IDs
    AppendMenu(hMenu, MF_STRING | checkSnapTap, ID_TRAY_TOGGLE_SNAPTAP_ITEM, TEXT("Enable SnapTap (WASD strafing)"));
    AppendMenu(hMenu, MF_STRING | checkFlag, ID_TRAY_TOGGLE_SPAM_ITEM, TEXT("Enable Spam Mode"));
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT_MENU_ITEM, TEXT("Exit"));

    int commandId = TrackPopupMenu(hMenu,
        TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
        pt.x, pt.y, 0, hwnd, NULL);

    DestroyMenu(hMenu);

    // Use #define or Globals:: prefix for IDs
    switch (commandId) {
    case ID_TRAY_TOGGLE_SPAM_ITEM:
    {
        bool newState = !Config::EnableSpam.load(std::memory_order_relaxed);
        Config::EnableSpam.store(newState, std::memory_order_relaxed);
        std::cout << "Spam Mode " << (newState ? "Enabled" : "Disabled") << std::endl;
        if (!newState) {
            CleanupSpamState(false);
            RefreshMovementState();
        }
        Config::SaveConfig();
    }
    break;
    case ID_TRAY_TOGGLE_SNAPTAP_ITEM:
    {
        bool newState = !Config::EnableSnapTap.load(std::memory_order_relaxed);
        Config::EnableSnapTap.store(newState, std::memory_order_relaxed);
        std::cout << "SnapTap " << (newState ? "Enabled" : "Disabled") << std::endl;
        OnSnapTapToggled(newState);
        Config::SaveConfig();
    }
    break;
    case ID_TRAY_EXIT_MENU_ITEM:
        std::cout << "Exit command selected from tray menu." << std::endl;
        DestroyWindow(hwnd);
        break;
    }

    PostMessage(hwnd, WM_NULL, 0, 0);
}

void RemoveTrayIcon() {
    if (Globals::g_nid.hWnd) { // Globals::
        if (Shell_NotifyIcon(NIM_DELETE, &Globals::g_nid)) { // Globals::
            std::cout << "Tray icon removed." << std::endl;
        }
        else {
            LogError("Shell_NotifyIcon(NIM_DELETE) failed");
        }
        // DestroyIcon usually not needed for LoadImage+LR_SHARED or system icons
        Globals::g_nid.hWnd = NULL; // Globals::
    }
}
