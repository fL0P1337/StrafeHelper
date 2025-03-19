#include <windows.h>
#include <shellapi.h>
#include <iostream>
#include <cstdio>
#include <mutex>
#include "inputs.h"
#include "Config.h"
#include "superglide.h"

// Constants
#define ID_TRAY_APP_ICON                1001
#define ID_TRAY_EXIT_CONTEXT_MENU_ITEM  3000
#define ID_TRAY_WASD_STRAFING          3002
#define ID_TRAY_DELAY_1MS              3004
#define ID_TRAY_DELAY_2MS              3005
#define ID_TRAY_DELAY_5MS              3006
#define ID_TRAY_STARTUP                3007
#define ID_TRAY_CONSOLE                3008
#define ID_TRAY_SAVE_CONFIG            3009
#define ID_TRAY_RELOAD_CONFIG          3010
#define ID_TRAY_ABOUT                  3011
#define ID_TRAY_SUPERGLIDE             3012
#define ID_TRAY_TARGET_FPS_144         3013
#define ID_TRAY_TARGET_FPS_165         3014
#define ID_TRAY_TARGET_FPS_240         3015
#define WM_TRAYICON                    (WM_USER + 1)

const char* APP_NAME = "StrafeHelper";
const char* VERSION = "3.0_game_compatible";
const char* AUTHOR = "fL0P1337";
const char* CURRENT_TIME = "2025-03-18 11:04:52";
const char* CURRENT_USER = "fL0P1337";

NOTIFYICONDATA nid;

void InitNotifyIconData(HWND hwnd) {
    memset(&nid, 0, sizeof(NOTIFYICONDATA));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = ID_TRAY_APP_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    Shell_NotifyIcon(NIM_ADD, &nid);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HMENU hMenu = NULL;
    static HMENU hSettingsMenu = NULL;
    static HMENU hDelayMenu = NULL;
    static HMENU hFPSMenu = NULL;
    static FILE* dummy;

    switch (msg) {
    case WM_CREATE: {
        hMenu = CreatePopupMenu();
        hSettingsMenu = CreatePopupMenu();
        hDelayMenu = CreatePopupMenu();
        hFPSMenu = CreatePopupMenu();

        // Delay submenu
        AppendMenu(hDelayMenu, MF_STRING, ID_TRAY_DELAY_1MS, TEXT("1ms (Fastest)"));
        AppendMenu(hDelayMenu, MF_STRING, ID_TRAY_DELAY_2MS, TEXT("2ms (Balanced)"));
        AppendMenu(hDelayMenu, MF_STRING, ID_TRAY_DELAY_5MS, TEXT("5ms (Stable)"));

        // FPS submenu
        AppendMenu(hFPSMenu, MF_STRING, ID_TRAY_TARGET_FPS_144, TEXT("144 FPS"));
        AppendMenu(hFPSMenu, MF_STRING, ID_TRAY_TARGET_FPS_165, TEXT("165 FPS"));
        AppendMenu(hFPSMenu, MF_STRING, ID_TRAY_TARGET_FPS_240, TEXT("240 FPS"));

        // Settings submenu
        AppendMenu(hSettingsMenu, MF_STRING | MF_POPUP, (UINT_PTR)hDelayMenu, TEXT("Spam Delay"));
        AppendMenu(hSettingsMenu, MF_STRING | MF_POPUP, (UINT_PTR)hFPSMenu, TEXT("Target FPS"));
        AppendMenu(hSettingsMenu, MF_SEPARATOR, 0, NULL);
        AppendMenu(hSettingsMenu, MF_STRING, ID_TRAY_STARTUP, TEXT("Start with Windows"));
        AppendMenu(hSettingsMenu, MF_STRING, ID_TRAY_CONSOLE, TEXT("Show Console"));
        AppendMenu(hSettingsMenu, MF_STRING, ID_TRAY_SAVE_CONFIG, TEXT("Save Configuration"));
        AppendMenu(hSettingsMenu, MF_STRING, ID_TRAY_RELOAD_CONFIG, TEXT("Reload Configuration"));

        // Main menu
        AppendMenu(hMenu, MF_STRING, ID_TRAY_WASD_STRAFING, TEXT("WASD Strafing"));
        AppendMenu(hMenu, MF_STRING, ID_TRAY_SUPERGLIDE, TEXT("SuperGlide"));
        AppendMenu(hMenu, MF_STRING | MF_POPUP, (UINT_PTR)hSettingsMenu, TEXT("Settings"));
        AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenu(hMenu, MF_STRING, ID_TRAY_ABOUT, TEXT("About"));
        AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT_CONTEXT_MENU_ITEM, TEXT("Exit"));
        break;
    }

    case WM_TRAYICON: {
        if (lParam == WM_RBUTTONDOWN) {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);

            // Update checkmarks
            CheckMenuItem(hMenu, ID_TRAY_WASD_STRAFING,
                Config::getInstance()->isWASDStrafingEnabled.load() ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(hMenu, ID_TRAY_SUPERGLIDE,
                Config::getInstance()->isSuperGlideEnabled.load() ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(hSettingsMenu, ID_TRAY_STARTUP,
                Config::getInstance()->startWithWindows.load() ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(hSettingsMenu, ID_TRAY_CONSOLE,
                Config::getInstance()->showConsole.load() ? MF_CHECKED : MF_UNCHECKED);

            CheckMenuItem(hDelayMenu, ID_TRAY_DELAY_1MS,
                Config::getInstance()->spamDelayMs.load() == 1 ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(hDelayMenu, ID_TRAY_DELAY_2MS,
                Config::getInstance()->spamDelayMs.load() == 2 ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(hDelayMenu, ID_TRAY_DELAY_5MS,
                Config::getInstance()->spamDelayMs.load() == 5 ? MF_CHECKED : MF_UNCHECKED);

            CheckMenuItem(hFPSMenu, ID_TRAY_TARGET_FPS_144,
                Config::getInstance()->targetFPS.load() == 144 ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(hFPSMenu, ID_TRAY_TARGET_FPS_165,
                Config::getInstance()->targetFPS.load() == 165 ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(hFPSMenu, ID_TRAY_TARGET_FPS_240,
                Config::getInstance()->targetFPS.load() == 240 ? MF_CHECKED : MF_UNCHECKED);

            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                pt.x, pt.y, 0, hwnd, NULL);
        }
        break;
    }

    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case ID_TRAY_EXIT_CONTEXT_MENU_ITEM:
            DestroyWindow(hwnd);
            break;

        case ID_TRAY_WASD_STRAFING: {
            bool newState = !Config::getInstance()->isWASDStrafingEnabled.load();
            Config::getInstance()->isWASDStrafingEnabled.store(newState);
            Config::getInstance()->save();
            break;
        }

        case ID_TRAY_SUPERGLIDE: {
            bool newState = !Config::getInstance()->isSuperGlideEnabled.load();
            Config::getInstance()->isSuperGlideEnabled.store(newState);
            Config::getInstance()->save();
            if (Config::getInstance()->showConsole.load()) {
                std::cout << "SuperGlide " << (newState ? "enabled" : "disabled")
                    << " at " << CURRENT_TIME << " UTC\n";
            }
            break;
        }

        case ID_TRAY_DELAY_1MS:
            Config::getInstance()->spamDelayMs.store(1);
            Config::getInstance()->spamKeyDownDuration.store(1);
            Config::getInstance()->save();
            break;

        case ID_TRAY_DELAY_2MS:
            Config::getInstance()->spamDelayMs.store(2);
            Config::getInstance()->spamKeyDownDuration.store(1);
            Config::getInstance()->save();
            break;

        case ID_TRAY_DELAY_5MS:
            Config::getInstance()->spamDelayMs.store(5);
            Config::getInstance()->spamKeyDownDuration.store(2);
            Config::getInstance()->save();
            break;

        case ID_TRAY_TARGET_FPS_144:
            Config::getInstance()->targetFPS.store(144);
            Config::getInstance()->save();
            break;

        case ID_TRAY_TARGET_FPS_165:
            Config::getInstance()->targetFPS.store(165);
            Config::getInstance()->save();
            break;

        case ID_TRAY_TARGET_FPS_240:
            Config::getInstance()->targetFPS.store(240);
            Config::getInstance()->save();
            break;

        case ID_TRAY_STARTUP:
            Config::getInstance()->setStartWithWindows(
                !Config::getInstance()->startWithWindows.load());
            break;

        case ID_TRAY_CONSOLE: {
            bool newState = !Config::getInstance()->showConsole.load();
            Config::getInstance()->showConsole.store(newState);
            if (newState) {
                AllocConsole();
                FILE* dummy;
                freopen_s(&dummy, "CONOUT$", "w", stdout);
                SetConsoleTitleA(APP_NAME);

                std::cout << "\nStrafeHelper Status Update - " << CURRENT_TIME << " UTC\n";
                std::cout << "User: " << CURRENT_USER << "\n";
                std::cout << "Current Settings:\n";
                std::cout << "- Spam Delay: " << Config::getInstance()->spamDelayMs.load() << "ms\n";
                std::cout << "- Key Down Duration: " << Config::getInstance()->spamKeyDownDuration.load() << "ms\n";
                std::cout << "- WASD Strafing: " << (Config::getInstance()->isWASDStrafingEnabled.load() ? "Enabled" : "Disabled") << "\n";
                std::cout << "- SuperGlide: " << (Config::getInstance()->isSuperGlideEnabled.load() ? "Enabled" : "Disabled") << "\n";
                std::cout << "- Target FPS: " << Config::getInstance()->targetFPS.load() << "\n";
                std::cout << "- Trigger Key: " << static_cast<char>(Config::getInstance()->spamTriggerKey.load()) << "\n";
            }
            else {
                FreeConsole();
            }
            Config::getInstance()->save();
            break;
        }

        case ID_TRAY_SAVE_CONFIG:
            if (Config::getInstance()->save()) {
                if (Config::getInstance()->showConsole.load()) {
                    std::cout << "Configuration saved successfully at " << CURRENT_TIME << " UTC\n";
                }
                MessageBox(hwnd, TEXT("Configuration saved successfully!"),
                    TEXT("Success"), MB_ICONINFORMATION | MB_OK);
            }
            break;

        case ID_TRAY_RELOAD_CONFIG:
            if (Config::getInstance()->load()) {
                if (Config::getInstance()->showConsole.load()) {
                    std::cout << "Configuration reloaded successfully at " << CURRENT_TIME << " UTC\n";
                }
                MessageBox(hwnd, TEXT("Configuration reloaded successfully!"),
                    TEXT("Success"), MB_ICONINFORMATION | MB_OK);
            }
            break;

        case ID_TRAY_ABOUT: {
            std::string aboutMsg =
                "StrafeHelper v" + std::string(VERSION) + "\n"
                "Created by: " + std::string(AUTHOR) + "\n"
                "Current User: " + std::string(CURRENT_USER) + "\n"
                "Time: " + std::string(CURRENT_TIME) + " UTC\n\n"
                "Features:\n"
                "- Hold C + WASD for enhanced strafing\n"
                "- SuperGlide with configurable FPS\n"
                "Right-click tray icon for settings";

            MessageBoxA(hwnd, aboutMsg.c_str(), "About StrafeHelper", MB_ICONINFORMATION | MB_OK);
            break;
        }
        }
        break;
    }

    case WM_DESTROY: {
        if (hFPSMenu) DestroyMenu(hFPSMenu);
        if (hDelayMenu) DestroyMenu(hDelayMenu);
        if (hSettingsMenu) DestroyMenu(hSettingsMenu);
        if (hMenu) DestroyMenu(hMenu);
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int main() {
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    Config::getInstance()->load();

    if (Config::getInstance()->showConsole.load()) {
        AllocConsole();
        FILE* dummy;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        SetConsoleTitleA(APP_NAME);

        std::cout << APP_NAME << " v" << VERSION << " started!\n";
        std::cout << "Created by: " << AUTHOR << "\n";
        std::cout << "Current time (UTC): " << CURRENT_TIME << "\n\n";
        std::cout << "Controls:\n";
        std::cout << "- Press C + W/A/S/D for optimized game strafing\n";
        std::cout << "- SuperGlide available with configurable FPS\n";
    }

    auto inputManager = inputs::InputManager::getInstance();

    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = TEXT("StrafeHelperClass");
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, TEXT("Window Registration Failed!"), TEXT("Error"),
            MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }

    HWND hwnd = CreateWindowEx(
        0,
        wc.lpszClassName,
        TEXT("StrafeHelper"),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        240, 120,
        NULL, NULL,
        wc.hInstance,
        NULL
    );

    if (!hwnd) {
        MessageBox(NULL, TEXT("Window Creation Failed!"), TEXT("Error"),
            MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }

    if (!inputManager->initRawInput(hwnd)) {
        if (Config::getInstance()->showConsole.load()) {
        std::cout << "Raw Input initialization failed. Some games may not work properly.\n";
        }
            
    }

    InitNotifyIconData(hwnd);

    if (!inputManager->setupKeyboardHook()) {
        MessageBox(NULL, TEXT("Failed to install keyboard hook!"),
            TEXT("Error"), MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }

    if (!inputManager->initializeEvents()) {
        MessageBox(NULL, TEXT("Failed to initialize events!"),
            TEXT("Error"), MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }

    if (Config::getInstance()->showConsole.load()) {
        std::cout << "\nConfiguration:\n";
        std::cout << "-------------\n";
        std::cout << "User: " << CURRENT_USER << "\n";
        std::cout << "Start Time: " << "2025-03-18 11:06:27" << " UTC\n";
        std::cout << "Spam Delay: " << Config::getInstance()->spamDelayMs.load() << "ms\n";
        std::cout << "Key Down Duration: " << Config::getInstance()->spamKeyDownDuration.load() << "ms\n";
        std::cout << "WASD Strafing: " << (Config::getInstance()->isWASDStrafingEnabled.load() ? "Enabled" : "Disabled") << "\n";
        std::cout << "SuperGlide: " << (Config::getInstance()->isSuperGlideEnabled.load() ? "Enabled" : "Disabled") << "\n";
        std::cout << "Target FPS: " << Config::getInstance()->targetFPS.load() << "\n";
        std::cout << "Trigger Key: " << static_cast<char>(Config::getInstance()->spamTriggerKey.load()) << "\n\n";
        std::cout << "Game Compatibility Mode: Enabled\n";
        std::cout << "Raw Input Status: Active\n";
        std::cout << "Process Priority: High\n\n";
        std::cout << "Controls:\n";
        std::cout << "- Right-click tray icon for menu\n";
        std::cout << "- Hold C + WASD for enhanced strafing\n";
        std::cout << "- SuperGlide with configurable FPS settings\n";
        std::cout << "- ESC to exit\n\n";
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
            break;
        }
    }

    if (Config::getInstance()->showConsole.load()) {
        std::cout << "\nShutting down...\n";
        std::cout << "End Time: " << "2025-03-18 11:06:27" << " UTC\n";
        std::cout << "User: " << CURRENT_USER << "\n";
        std::cout << "Final Settings:\n";
        std::cout << "- Spam Delay: " << Config::getInstance()->spamDelayMs.load() << "ms\n";
        std::cout << "- WASD Strafing: " << (Config::getInstance()->isWASDStrafingEnabled.load() ? "Enabled" : "Disabled") << "\n";
        std::cout << "- SuperGlide: " << (Config::getInstance()->isSuperGlideEnabled.load() ? "Enabled" : "Disabled") << "\n";
        std::cout << "- Target FPS: " << Config::getInstance()->targetFPS.load() << "\n";
    }

    // Save final configuration
    Config::getInstance()->save();

    // Cleanup input manager
    inputManager->cleanup();

    // Small delay to show cleanup messages
    if (Config::getInstance()->showConsole.load()) {
        std::cout << "Cleanup complete. Thanks for using " << APP_NAME << "!\n";
        std::cout << "Session ended at: " << "2025-03-18 11:06:27" << " UTC\n";
        std::cout << "User: " << CURRENT_USER << "\n";
        Sleep(1500);
        FreeConsole();
    }

    return 0;
}