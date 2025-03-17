#include <windows.h>
#include <shellapi.h>
#include <map>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iostream>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <thread>
#include <dinput.h>
#include "config.h"

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

// Constants for tray icon and menu
#define ID_TRAY_APP_ICON                1001
#define ID_TRAY_EXIT_CONTEXT_MENU_ITEM  3000
#define ID_TRAY_WASD_STRAFING           3002
#define WM_TRAYICON                     (WM_USER + 1)
#define ID_TRAY_SETTINGS_MENU           3003
#define ID_TRAY_DELAY_1MS               3004
#define ID_TRAY_DELAY_2MS               3005
#define ID_TRAY_DELAY_5MS               3006
#define ID_TRAY_STARTUP                 3007
#define ID_TRAY_CONSOLE                 3008
#define ID_TRAY_SAVE_CONFIG             3009
#define ID_TRAY_RELOAD_CONFIG           3010
#define ID_TRAY_ABOUT                   3011



// Application details
const char* APP_NAME = "StrafeHelper";
const char* VERSION = "3.0_game_compatible";
const char* AUTHOR = "fL0P1337";

// Configuration defaults with atomic variables for thread safety
std::atomic<int> SPAM_DELAY_MS{ 1 };         // Optimized delay for game compatibility
std::atomic<int> SPAM_KEY_DOWN_DURATION{ 1 }; // Optimized duration for games

// Global configurable atomics
std::atomic<bool> isLocked{ false };
std::atomic<bool> isCSpamActive{ false };
std::atomic<bool> isWASDStrafingEnabled{ true };
std::atomic<int> KEY_SPAM_TRIGGER{ 'C' };

// DirectInput variables
LPDIRECTINPUT8 g_pDI = nullptr;
LPDIRECTINPUTDEVICE8 g_pKeyboard = nullptr;

// Pre-allocated input buffers
INPUT keyDownInputs[4];
INPUT keyUpInputs[4];
INPUT scanCodeInputs[4];

// Enhanced key state tracking
struct KeyState {
    std::atomic<bool> physicalKeyDown;
    std::atomic<bool> spamActive;
    std::atomic<bool> wasPhysicallyDown;  // New: Track previous physical state
    std::chrono::steady_clock::time_point lastStateChange;

    KeyState() : physicalKeyDown(false), spamActive(false), wasPhysicallyDown(false) {
        lastStateChange = std::chrono::steady_clock::now();
    }
};

// Global variables
HHOOK hHook = NULL;
NOTIFYICONDATA nid;
std::unordered_map<int, KeyState*> KeyInfo;
std::vector<int> activeSpamKeys;
std::mutex spamKeysMutex;

HANDLE hSpamEvent = NULL;
HANDLE hTerminateEvent = NULL;
HANDLE hSpamThreadHandle = NULL;

// Forward declarations
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Enhanced input sending function for game compatibility
void SendGameInput(int targetKey, bool keyDown) {
    INPUT input = { 0 };
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = targetKey;
    input.ki.wScan = MapVirtualKey(targetKey, 0);
    input.ki.dwFlags = keyDown ? 0 : KEYEVENTF_KEYUP;
    input.ki.time = 0;
    input.ki.dwExtraInfo = 0;
    SendInput(1, &input, sizeof(INPUT));
}

void RestoreKeyStates() {
    std::lock_guard<std::mutex> lock(spamKeysMutex);

    // First release all keys that were being spammed
    for (int key : activeSpamKeys) {
        SendGameInput(key, false);
    }

    Sleep(1);  // Small delay to ensure releases are processed

    // Then restore only the keys that are still physically held
    for (int key : activeSpamKeys) {
        auto keyState = KeyInfo.find(key);
        if (keyState != KeyInfo.end()) {
            keyState->second->spamActive = false;
            if (keyState->second->physicalKeyDown) {
                SendGameInput(key, true);
            }
        }
    }

    activeSpamKeys.clear();
}


// Initialize DirectInput
bool InitDirectInput(HWND hwnd) {
    if (FAILED(DirectInput8Create(GetModuleHandle(NULL), DIRECTINPUT_VERSION,
        IID_IDirectInput8, (void**)&g_pDI, NULL))) {
        return false;
    }

    if (FAILED(g_pDI->CreateDevice(GUID_SysKeyboard, &g_pKeyboard, NULL))) {
        return false;
    }

    if (FAILED(g_pKeyboard->SetDataFormat(&c_dfDIKeyboard))) {
        return false;
    }

    if (FAILED(g_pKeyboard->SetCooperativeLevel(hwnd,
        DISCL_FOREGROUND | DISCL_NONEXCLUSIVE))) {
        return false;
    }

    g_pKeyboard->Acquire();
    return true;
}

// Modified KeyboardProc to fix key state restoration
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < 0) {
        return CallNextHookEx(hHook, nCode, wParam, lParam);
    }

    KBDLLHOOKSTRUCT* pKeybd = (KBDLLHOOKSTRUCT*)lParam;
    int keyCode = pKeybd->vkCode;
    bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    bool isKeyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
    bool isInjected = (pKeybd->flags & LLKHF_INJECTED);

    // Fast path for non-relevant keys
    if (keyCode != 'W' && keyCode != 'A' && keyCode != 'S' &&
        keyCode != 'D' && keyCode != KEY_SPAM_TRIGGER) {
        return CallNextHookEx(hHook, nCode, wParam, lParam);
    }

    // Handle WASD keys
    if (keyCode == 'W' || keyCode == 'A' || keyCode == 'S' || keyCode == 'D') {
        auto keyState = KeyInfo.find(keyCode);
        if (keyState != KeyInfo.end() && !isInjected) {
            // Update physical state
            keyState->second->physicalKeyDown = isKeyDown;

            if (isCSpamActive) {
                std::lock_guard<std::mutex> lock(spamKeysMutex);
                if (isKeyDown) {
                    // Add key to spam if not already present
                    if (std::find(activeSpamKeys.begin(), activeSpamKeys.end(), keyCode) == activeSpamKeys.end()) {
                        activeSpamKeys.push_back(keyCode);
                        keyState->second->spamActive = true;
                    }
                }
                else {
                    // Key up - remove from spam and ensure it's released
                    auto it = std::find(activeSpamKeys.begin(), activeSpamKeys.end(), keyCode);
                    if (it != activeSpamKeys.end()) {
                        activeSpamKeys.erase(it);
                        keyState->second->spamActive = false;
                        // Ensure key is fully released
                        SendGameInput(keyCode, false);
                        // Small delay to ensure release is processed
                        Sleep(1);
                    }
                }
                SetEvent(hSpamEvent);
                return 1;
            }
        }
    }

    // Handle spam trigger key (C)
    if (keyCode == KEY_SPAM_TRIGGER && !isInjected) {
        if (isKeyDown && !isCSpamActive) {
            isCSpamActive = true;
            {
                std::lock_guard<std::mutex> lock(spamKeysMutex);
                activeSpamKeys.clear();
                // Add currently held keys
                for (int key : { 'W', 'A', 'S', 'D' }) {
                    auto keyState = KeyInfo.find(key);
                    if (keyState != KeyInfo.end() && keyState->second->physicalKeyDown) {
                        activeSpamKeys.push_back(key);
                        keyState->second->spamActive = true;
                    }
                }
            }
            SetEvent(hSpamEvent);
        }
        else if (isKeyUp && isCSpamActive) {
            isCSpamActive = false;
            RestoreKeyStates();
            SetEvent(hSpamEvent);
        }
    }

    return CallNextHookEx(hHook, nCode, wParam, lParam);
}

// Modified spam thread to ensure synchronous key spamming

DWORD WINAPI SpamThread(LPVOID lpParam) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    SetThreadAffinityMask(GetCurrentThread(), 1);

    std::vector<int> keysToSpam;
    HANDLE events[2] = { hSpamEvent, hTerminateEvent };

    while (true) {
        DWORD waitResult = WaitForMultipleObjects(2, events, FALSE, SPAM_DELAY_MS);

        if (waitResult == WAIT_OBJECT_0 + 1) {
            break;
        }

        if (isWASDStrafingEnabled && isCSpamActive) {
            {
                std::lock_guard<std::mutex> lock(spamKeysMutex);
                keysToSpam = activeSpamKeys;  // Copy current active keys
            }

            if (!keysToSpam.empty()) {
                // Release all active keys
                for (int key : keysToSpam) {
                    auto keyState = KeyInfo.find(key);
                    if (keyState != KeyInfo.end() && keyState->second->spamActive) {
                        SendGameInput(key, false);
                    }
                }

                Sleep(SPAM_KEY_DOWN_DURATION);

                // Press only keys that are still active
                for (int key : keysToSpam) {
                    auto keyState = KeyInfo.find(key);
                    if (keyState != KeyInfo.end() &&
                        keyState->second->spamActive &&
                        keyState->second->physicalKeyDown) {
                        SendGameInput(key, true);
                    }
                }
            }
        }
    }
    return 0;
}

// Initialize tray icon
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

// Window procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HMENU hMenu = NULL;
    static HMENU hSettingsMenu = NULL;
    static HMENU hDelayMenu = NULL;
    static FILE* dummy;

    switch (msg) {
    case WM_CREATE: {
        // Create main popup menu
        hMenu = CreatePopupMenu();
        hSettingsMenu = CreatePopupMenu();
        hDelayMenu = CreatePopupMenu();

        // Create Delay submenu
        AppendMenu(hDelayMenu, MF_STRING, ID_TRAY_DELAY_1MS, TEXT("1ms (Fastest)"));
        AppendMenu(hDelayMenu, MF_STRING, ID_TRAY_DELAY_2MS, TEXT("2ms (Balanced)"));
        AppendMenu(hDelayMenu, MF_STRING, ID_TRAY_DELAY_5MS, TEXT("5ms (Stable)"));

        // Create Settings submenu
        AppendMenu(hSettingsMenu, MF_STRING | MF_POPUP, (UINT_PTR)hDelayMenu, TEXT("Spam Delay"));
        AppendMenu(hSettingsMenu, MF_SEPARATOR, 0, NULL);
        AppendMenu(hSettingsMenu, MF_STRING, ID_TRAY_STARTUP, TEXT("Start with Windows"));
        AppendMenu(hSettingsMenu, MF_STRING, ID_TRAY_CONSOLE, TEXT("Show Console"));
        AppendMenu(hSettingsMenu, MF_STRING, ID_TRAY_SAVE_CONFIG, TEXT("Save Configuration"));
        AppendMenu(hSettingsMenu, MF_STRING, ID_TRAY_RELOAD_CONFIG, TEXT("Reload Configuration"));

        // Main menu items
        AppendMenu(hMenu, MF_STRING, ID_TRAY_WASD_STRAFING, TEXT("WASD Strafing"));
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

            // Update menu check states
            CheckMenuItem(hMenu, ID_TRAY_WASD_STRAFING,
                Config::getInstance()->isWASDStrafingEnabled ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(hSettingsMenu, ID_TRAY_STARTUP,
                Config::getInstance()->startWithWindows ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(hSettingsMenu, ID_TRAY_CONSOLE,
                Config::getInstance()->showConsole ? MF_CHECKED : MF_UNCHECKED);

            // Update delay menu checks
            CheckMenuItem(hDelayMenu, ID_TRAY_DELAY_1MS,
                Config::getInstance()->spamDelayMs == 1 ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(hDelayMenu, ID_TRAY_DELAY_2MS,
                Config::getInstance()->spamDelayMs == 2 ? MF_CHECKED : MF_UNCHECKED);
            CheckMenuItem(hDelayMenu, ID_TRAY_DELAY_5MS,
                Config::getInstance()->spamDelayMs == 5 ? MF_CHECKED : MF_UNCHECKED);

            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                pt.x, pt.y, 0, hwnd, NULL);
        }
        break;
    }

    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case ID_TRAY_EXIT_CONTEXT_MENU_ITEM: {
            DestroyWindow(hwnd);
            break;
        }

        case ID_TRAY_WASD_STRAFING: {
            bool newState = !Config::getInstance()->isWASDStrafingEnabled.load();
            Config::getInstance()->isWASDStrafingEnabled.store(newState);
            isWASDStrafingEnabled.store(newState);

            if (!newState) {
                std::lock_guard<std::mutex> lock(spamKeysMutex);
                for (auto& pair : KeyInfo) {
                    pair.second->spamActive = false;
                }
                activeSpamKeys.clear();
                isCSpamActive = false;
                SetEvent(hSpamEvent);
            }
            Config::getInstance()->save();
            break;
        }

        case ID_TRAY_DELAY_1MS: {
            Config::getInstance()->spamDelayMs.store(1);
            Config::getInstance()->spamKeyDownDuration.store(1);
            SPAM_DELAY_MS.store(1);
            SPAM_KEY_DOWN_DURATION.store(1);
            Config::getInstance()->save();
            break;
        }

        case ID_TRAY_DELAY_2MS: {
            Config::getInstance()->spamDelayMs = 2;
            Config::getInstance()->spamKeyDownDuration = 1;
            SPAM_DELAY_MS = 2;
            SPAM_KEY_DOWN_DURATION = 1;
            Config::getInstance()->save();
            break;
        }

        case ID_TRAY_DELAY_5MS: {
            Config::getInstance()->spamDelayMs = 5;
            Config::getInstance()->spamKeyDownDuration = 2;
            SPAM_DELAY_MS = 5;
            SPAM_KEY_DOWN_DURATION = 2;
            Config::getInstance()->save();
            break;
        }

        case ID_TRAY_STARTUP: {
            Config::getInstance()->setStartWithWindows(
                !Config::getInstance()->startWithWindows);
            break;
        }

        case ID_TRAY_CONSOLE: {
            Config::getInstance()->showConsole = !Config::getInstance()->showConsole;
            if (Config::getInstance()->showConsole) {
                AllocConsole();
                freopen_s(&dummy, "CONOUT$", "w", stdout);
                SetConsoleTitleA(APP_NAME);

                // Print current status
                std::cout << "\nStrafeHelper Status Update - " << "2025-03-17 22:32:05" << " UTC\n";
                std::cout << "User: " << "fL0P1337" << "\n";
                std::cout << "Current Settings:\n";
                std::cout << "- Spam Delay: " << Config::getInstance()->spamDelayMs << "ms\n";
                std::cout << "- Key Down Duration: " << Config::getInstance()->spamKeyDownDuration << "ms\n";
                std::cout << "- WASD Strafing: " << (Config::getInstance()->isWASDStrafingEnabled ? "Enabled" : "Disabled") << "\n";
                std::cout << "- Trigger Key: " << static_cast<char>(Config::getInstance()->spamTriggerKey) << "\n";
            }
            else {
                FreeConsole();
            }
            Config::getInstance()->save();
            break;
        }

        case ID_TRAY_SAVE_CONFIG: {
            if (Config::getInstance()->save()) {
                if (Config::getInstance()->showConsole) {
                    std::cout << "Configuration saved successfully at " << "2025-03-17 22:32:05" << " UTC\n";
                }
                MessageBox(hwnd, TEXT("Configuration saved successfully!"),
                    TEXT("Success"), MB_ICONINFORMATION | MB_OK);
            }
            break;
        }

        case ID_TRAY_RELOAD_CONFIG: {
            if (Config::getInstance()->load()) {
                // Update global variables using atomic store()
                SPAM_DELAY_MS.store(Config::getInstance()->spamDelayMs.load());
                SPAM_KEY_DOWN_DURATION.store(Config::getInstance()->spamKeyDownDuration.load());
                isWASDStrafingEnabled.store(Config::getInstance()->isWASDStrafingEnabled.load());
                KEY_SPAM_TRIGGER.store(Config::getInstance()->spamTriggerKey.load());

                if (Config::getInstance()->showConsole.load()) {
                    std::cout << "Configuration reloaded successfully at " << "2025-03-17 22:38:23" << " UTC\n";
                    std::cout << "Current user: " << "fL0P1337" << "\n";
                    std::cout << "Updated settings:\n";
                    std::cout << "- Spam Delay: " << SPAM_DELAY_MS.load() << "ms\n";
                    std::cout << "- Key Down Duration: " << SPAM_KEY_DOWN_DURATION.load() << "ms\n";
                    std::cout << "- WASD Strafing: " << (isWASDStrafingEnabled.load() ? "Enabled" : "Disabled") << "\n";
                    std::cout << "- Trigger Key: " << static_cast<char>(KEY_SPAM_TRIGGER.load()) << "\n";
                }
                MessageBox(hwnd, TEXT("Configuration reloaded successfully!"),
                    TEXT("Success"), MB_ICONINFORMATION | MB_OK);
            }
            break;
        }

        case ID_TRAY_ABOUT: {
            std::string aboutMsg =
                "StrafeHelper v" + std::string(VERSION) + "\n"
                "Created by: " + std::string(AUTHOR) + "\n"
                "Current User: " + std::string("fL0P1337") + "\n"
                "Time: " + std::string("2025-03-17 22:32:05") + " UTC\n\n"
                "Hold C + WASD for enhanced strafing\n"
                "Right-click tray icon for settings";

            MessageBoxA(hwnd, aboutMsg.c_str(), "About StrafeHelper", MB_ICONINFORMATION | MB_OK);
            break;
        }
        }
        break;
    }

    case WM_DESTROY: {
        if (hDelayMenu) DestroyMenu(hDelayMenu);
        if (hSettingsMenu) DestroyMenu(hSettingsMenu);
        if (hMenu) DestroyMenu(hMenu);
        PostQuitMessage(0);
        break;
    }

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int main() {
    // Load configuration first
    Config::getInstance()->load();

    // Use configuration values
    SPAM_DELAY_MS.store(Config::getInstance()->spamDelayMs.load());
    SPAM_KEY_DOWN_DURATION.store(Config::getInstance()->spamKeyDownDuration.load());
    isWASDStrafingEnabled.store(Config::getInstance()->isWASDStrafingEnabled.load());
    KEY_SPAM_TRIGGER.store(Config::getInstance()->spamTriggerKey.load());

    if (!Config::getInstance()->showConsole.load()) {
        FreeConsole();
    }
    // Set process priority
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    // Initialize console
    AllocConsole();
    FILE* dummy;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    SetConsoleTitleA(APP_NAME);

    // Print startup information
    std::cout << APP_NAME << " v" << VERSION << " started!\n";
    std::cout << "Created by: " << AUTHOR << "\n";
    std::cout << "Current time (UTC): 2025-03-17 21:51:48\n\n";
    std::cout << "Press C + W/A/S/D for optimized game strafing.\n";

    // Initialize key states
    KeyInfo['W'] = new KeyState();
    KeyInfo['A'] = new KeyState();
    KeyInfo['S'] = new KeyState();
    KeyInfo['D'] = new KeyState();

    // Create events
    hSpamEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    hTerminateEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    // Register window class
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = TEXT("StrafeHelperClass");
    // Continuing from the window class registration...
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, TEXT("Window Registration Failed!"), TEXT("Error"),
            MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }

    // Create window
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

    // Initialize DirectInput
    if (!InitDirectInput(hwnd)) {
        std::cout << "DirectInput initialization failed. Some games may not work properly.\n";
    }

    // Initialize tray icon
    InitNotifyIconData(hwnd);

    // Set up keyboard hook
    hHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, NULL, 0);
    if (!hHook) {
        MessageBox(NULL, TEXT("Failed to install keyboard hook!"),
            TEXT("Error"), MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }

    // Start spam thread
    hSpamThreadHandle = CreateThread(NULL, 0, SpamThread, NULL, 0, NULL);
    SetThreadPriority(hSpamThreadHandle, THREAD_PRIORITY_TIME_CRITICAL);

    // Print configuration
    std::cout << "\nConfiguration:\n";
    std::cout << "-------------\n";
    std::cout << "User: " << "fL0P1337" << "\n";
    std::cout << "Start Time: " << "2025-03-17 21:53:42" << " UTC\n";
    std::cout << "Spam Delay: " << SPAM_DELAY_MS << "ms\n";
    std::cout << "Key Down Duration: " << SPAM_KEY_DOWN_DURATION << "ms\n";
    std::cout << "WASD Strafing: " << (isWASDStrafingEnabled ? "Enabled" : "Disabled") << "\n";
    std::cout << "Trigger Key: " << static_cast<char>(KEY_SPAM_TRIGGER) << "\n\n";
    std::cout << "Game Compatibility Mode: Enabled\n";
    std::cout << "DirectInput Status: " << (g_pDI ? "Active" : "Inactive") << "\n";
    std::cout << "Process Priority: High\n\n";
    std::cout << "Controls:\n";
    std::cout << "- Right-click tray icon for menu\n";
    std::cout << "- Hold C + WASD for enhanced strafing\n";
    std::cout << "- ESC to exit\n\n";

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);

        // Check for ESC key to exit
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
            break;
        }
    }

    // Cleanup
    std::cout << "Shutting down...\n";

    // Signal thread termination
    SetEvent(hTerminateEvent);
    if (hSpamThreadHandle) {
        WaitForSingleObject(hSpamThreadHandle, 1000);
        CloseHandle(hSpamThreadHandle);
    }

    // Clean up DirectInput
    if (g_pKeyboard) {
        g_pKeyboard->Unacquire();
        g_pKeyboard->Release();
        g_pKeyboard = nullptr;
    }
    if (g_pDI) {
        g_pDI->Release();
        g_pDI = nullptr;
    }

    // Remove keyboard hook
    if (hHook) {
        UnhookWindowsHookEx(hHook);
    }

    // Clean up events
    if (hSpamEvent) CloseHandle(hSpamEvent);
    if (hTerminateEvent) CloseHandle(hTerminateEvent);

    // Clean up key states
    for (auto& pair : KeyInfo) {
        delete pair.second;
    }
    KeyInfo.clear();

    // Remove tray icon
    Shell_NotifyIcon(NIM_DELETE, &nid);

    std::cout << "Cleanup complete. Exiting...\n";
    std::cout << "End Time: " << "2025-03-17 21:53:42" << " UTC\n";
    std::cout << "Thanks for using " << APP_NAME << "!\n";

    // Small delay to show cleanup messages
    Sleep(1500);

    // Close console
    FreeConsole();

    return 0;
}

// Helper function to load configuration from file
void loadConfig() {
    std::ifstream configFile("config.cfg");
    if (!configFile.is_open()) {
        std::cout << "No config file found. Using defaults.\n";
        return;
    }

    std::string line;
    while (std::getline(configFile, line)) {
        if (line.empty() || line[0] == '#') continue;

        size_t pos = line.find('=');
        if (pos == std::string::npos) continue;

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        // Remove whitespace
        key.erase(remove_if(key.begin(), key.end(), isspace), key.end());
        value.erase(remove_if(value.begin(), value.end(), isspace), value.end());

        if (key == "SPAM_DELAY_MS")
            SPAM_DELAY_MS = std::stoi(value);
        else if (key == "SPAM_KEY_DOWN_DURATION")
            SPAM_KEY_DOWN_DURATION = std::stoi(value);
        else if (key == "isWASDStrafingEnabled")
            isWASDStrafingEnabled = (value == "true" || value == "1");
        else if (key == "KEY_SPAM_TRIGGER")
            KEY_SPAM_TRIGGER = static_cast<int>(value[0]);
    }
    configFile.close();
}

// Helper function to save configuration
void saveConfig() {
    std::ofstream configFile("config.cfg");
    if (!configFile.is_open()) {
        std::cout << "Unable to save configuration.\n";
        return;
    }

    configFile << "# StrafeHelper Configuration\n";
    configFile << "# Last updated: " << "2025-03-17 21:53:42" << " UTC\n";
    configFile << "# User: " << "fL0P1337" << "\n\n";

    configFile << "SPAM_DELAY_MS=" << SPAM_DELAY_MS << "\n";
    configFile << "SPAM_KEY_DOWN_DURATION=" << SPAM_KEY_DOWN_DURATION << "\n";
    configFile << "isWASDStrafingEnabled=" << (isWASDStrafingEnabled ? "true" : "false") << "\n";
    configFile << "KEY_SPAM_TRIGGER=" << static_cast<char>(KEY_SPAM_TRIGGER) << "\n";

    configFile.close();
}