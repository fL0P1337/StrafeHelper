#include <windows.h>
#include <shellapi.h>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>

// Constants for tray icon and menu
#define ID_TRAY_APP_ICON                1001
#define ID_TRAY_EXIT_CONTEXT_MENU_ITEM  3000
#define ID_TRAY_WASD_STRAFING           3002
#define WM_TRAYICON                     (WM_USER + 1)

const char* APP_NAME = "StrafeHelper";
const char* VERSION = "1.0_optimized";

// Configuration defaults (may be overwritten from config file)
int SPAM_DELAY_MS = 10;         // Delay between spamming cycles
int SPAM_KEY_DOWN_DURATION = 5; // Duration in ms keys remain down in a cycle

// Global toggles
bool isLocked = false;            // Global toggle: spam feature enabled/disabled
bool isCSpamActive = false;       // Spam trigger active flag
bool isWASDStrafingEnabled = true; // Toggle for WASD Strafing

// Keybind customization
int KEY_SPAM_TRIGGER = 'C';      // Default key to trigger WASD spamming

// Structure to track key states
struct KeyState {
    bool physicalKeyDown = false;  // Is the key physically pressed?
    bool spamActive = false;       // Should the key be spammed?
};

// Global variables
HHOOK hHook = NULL;
NOTIFYICONDATA nid = {};
std::unordered_map<int, KeyState> KeyInfo;  // Map for WASD keys that allows O(1) access.
std::unordered_set<int> activeSpamKeys;     // Set of keys currently active for strafing

// Event to signal changes to the spam thread
HANDLE hSpamEvent = NULL;

// Forward declarations
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void InitNotifyIconData(HWND hwnd);
void SendKey(int targetKey, bool keyDown);
DWORD WINAPI SpamThread(LPVOID lpParam);
void loadConfig();

// Loads configuration from "config.cfg" file.
void loadConfig() {
    std::ifstream configFile("config.cfg");
    if (!configFile.is_open()) {
        std::cout << "Config file not found. Using defaults." << std::endl;
        return;
    }
    std::cout << "Loading configuration from config.cfg..." << std::endl;
    std::string line;
    while (std::getline(configFile, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos == std::string::npos) continue; // skip malformed lines

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        // Remove whitespace (faster if using std::remove_if inline)
        key.erase(std::remove_if(key.begin(), key.end(), ::isspace), key.end());
        value.erase(std::remove_if(value.begin(), value.end(), ::isspace), value.end());

        if (key == "SPAM_DELAY_MS")
            SPAM_DELAY_MS = std::stoi(value);
        else if (key == "SPAM_KEY_DOWN_DURATION")
            SPAM_KEY_DOWN_DURATION = std::stoi(value);
        else if (key == "isLocked")
            isLocked = (value == "true" || value == "1");
        else if (key == "isWASDStrafingEnabled")
            isWASDStrafingEnabled = (value == "true" || value == "1");
        else if (key == "KEY_SPAM_TRIGGER")
            KEY_SPAM_TRIGGER = static_cast<int>(value[0]);
    }
    configFile.close();

    std::cout << "Configuration loaded:" << std::endl;
    std::cout << "  SPAM_DELAY_MS: " << SPAM_DELAY_MS << std::endl;
    std::cout << "  SPAM_KEY_DOWN_DURATION: " << SPAM_KEY_DOWN_DURATION << std::endl;
    std::cout << "  isLocked: " << (isLocked ? "true" : "false") << std::endl;
    std::cout << "  isWASDStrafingEnabled: " << (isWASDStrafingEnabled ? "true" : "false") << std::endl;
    std::cout << "  KEY_SPAM_TRIGGER: " << static_cast<char>(KEY_SPAM_TRIGGER) << std::endl;
}

// Entry point
int main() {
    // Allocate console for debug output.
    AllocConsole();
    SetConsoleTitleA(APP_NAME);

    loadConfig();

    // Create a manual-reset event. Using auto-reset is fine because we only need a simple signal.
    hSpamEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!hSpamEvent) {
        std::cerr << "Failed to create event" << std::endl;
        return 1;
    }

    // Window class registration.
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = TEXT("StrafeHelperClass");
    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, TEXT("Window Registration Failed!"), TEXT("Error"), MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }

    // Create dummy window.
    HWND hwnd = CreateWindowEx(0, wc.lpszClassName, TEXT("StrafeHelper"), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 240, 120,
        NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) {
        MessageBox(NULL, TEXT("Window Creation Failed!"), TEXT("Error"), MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }

    InitNotifyIconData(hwnd);

    // Initialize WASD keys states.
    for (int key : { 'W', 'A', 'S', 'D' }) {
        KeyInfo[key] = KeyState();
    }

    // Set the low-level keyboard hook.
    hHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, NULL, 0);
    if (!hHook) {
        MessageBox(NULL, TEXT("Failed to install keyboard hook!"), TEXT("Error"), MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }

    // Start the spam thread.
    HANDLE hThread = CreateThread(NULL, 0, SpamThread, NULL, 0, NULL);
    if (!hThread) {
        MessageBox(NULL, TEXT("Failed to create spam thread!"), TEXT("Error"), MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }

    // Message loop.
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup resources.
    UnhookWindowsHookEx(hHook);
    Shell_NotifyIcon(NIM_DELETE, &nid);
    CloseHandle(hSpamEvent);
    CloseHandle(hThread);
    return 0;
}

// Low-level keyboard hook procedure.
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        KBDLLHOOKSTRUCT* pKeybd = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        int keyCode = pKeybd->vkCode;

        // Update WASD key physical state.
        if (KeyInfo.find(keyCode) != KeyInfo.end()) {
            if (wParam == WM_KEYDOWN)
                KeyInfo[keyCode].physicalKeyDown = true;
            else if (wParam == WM_KEYUP)
                KeyInfo[keyCode].physicalKeyDown = false;
        }

        // Manage spam trigger for WASD keys.
        if (isWASDStrafingEnabled && keyCode == KEY_SPAM_TRIGGER) {
            if (wParam == WM_KEYDOWN) {
                isCSpamActive = true;
                // For each WASD key, if pressed and not already active, add it.
                for (int key : { 'W', 'A', 'S', 'D' }) {
                    if (KeyInfo[key].physicalKeyDown && !KeyInfo[key].spamActive) {
                        activeSpamKeys.insert(key);
                        KeyInfo[key].spamActive = true;
                    }
                }
                SetEvent(hSpamEvent);
            }
            else if (wParam == WM_KEYUP) {
                isCSpamActive = false;
                // On trigger release, send key-up for all active keys.
                for (int key : activeSpamKeys) {
                    SendKey(key, false);
                    KeyInfo[key].spamActive = false;
                }
                activeSpamKeys.clear();
                // Also clear physical key state for WASD keys.
                for (int key : { 'W', 'A', 'S', 'D' }) {
                    KeyInfo[key].physicalKeyDown = false;
                }
                SetEvent(hSpamEvent);
            }
            return CallNextHookEx(hHook, nCode, wParam, lParam);
        }

        // Block repeated physical WASD events during spamming (unless locked or injected).
        if (!isLocked && isCSpamActive && KeyInfo.find(keyCode) != KeyInfo.end() && !(pKeybd->flags & LLKHF_INJECTED)) {
            if (wParam == WM_KEYDOWN) {
                if (activeSpamKeys.find(keyCode) == activeSpamKeys.end()) {
                    activeSpamKeys.insert(keyCode);
                    KeyInfo[keyCode].spamActive = true;
                    SetEvent(hSpamEvent);
                }
                return 1;  // Block this event.
            }
            else if (wParam == WM_KEYUP) {
                if (activeSpamKeys.find(keyCode) != activeSpamKeys.end()) {
                    SendKey(keyCode, false);
                    KeyInfo[keyCode].spamActive = false;
                    activeSpamKeys.erase(keyCode);
                    SetEvent(hSpamEvent);
                }
                return 1;  // Block this event.
            }
        }
    }
    return CallNextHookEx(hHook, nCode, wParam, lParam);
}

// Spam thread sends grouped WASD key events.
DWORD WINAPI SpamThread(LPVOID lpParam) {
    // Use a fixed array of INPUT structures sized for our 4 keys.
    INPUT inputs[4];
    while (true) {
        // Wait for state change or timeout (cycles may occur every SPAM_DELAY_MS ms).
        WaitForSingleObject(hSpamEvent, SPAM_DELAY_MS);

        if (isWASDStrafingEnabled && isCSpamActive && !activeSpamKeys.empty()) {
            std::vector<int> keysToSpam;
            // Collect keys that are active.
            for (int key : activeSpamKeys) {
                if (KeyInfo[key].spamActive)
                    keysToSpam.push_back(key);
            }
            if (!keysToSpam.empty()) {
                int count = 0;
                // Build key-down events.
                for (int key : keysToSpam) {
                    inputs[count].type = INPUT_KEYBOARD;
                    inputs[count].ki.wVk = key;
                    inputs[count].ki.wScan = MapVirtualKey(key, 0);
                    inputs[count].ki.dwFlags = KEYEVENTF_SCANCODE;
                    inputs[count].ki.time = 0;
                    inputs[count].ki.dwExtraInfo = 0;
                    ++count;
                }
                if (count > 0)
                    SendInput(count, inputs, sizeof(INPUT));

                Sleep(SPAM_KEY_DOWN_DURATION);

                count = 0;
                // Build key-up events.
                for (int key : keysToSpam) {
                    inputs[count].type = INPUT_KEYBOARD;
                    inputs[count].ki.wVk = key;
                    inputs[count].ki.wScan = MapVirtualKey(key, 0);
                    inputs[count].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
                    inputs[count].ki.time = 0;
                    inputs[count].ki.dwExtraInfo = 0;
                    ++count;
                }
                if (count > 0)
                    SendInput(count, inputs, sizeof(INPUT));
            }
        }
    }
    return 0;
}

// Sends a key event.
void SendKey(int targetKey, bool keyDown) {
    INPUT input = { 0 };
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = targetKey;
    input.ki.wScan = MapVirtualKey(targetKey, 0);
    input.ki.dwFlags = KEYEVENTF_SCANCODE | (keyDown ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &input, sizeof(INPUT));
}

// Initialize system tray icon.
void InitNotifyIconData(HWND hwnd) {
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = ID_TRAY_APP_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    lstrcpy(nid.szTip, TEXT("StrafeHelper - Strafe"));
    Shell_NotifyIcon(NIM_ADD, &nid);
}

// Window procedure to handle tray icon and menu events.
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONDOWN) {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING | (isWASDStrafingEnabled ? MF_CHECKED : 0), ID_TRAY_WASD_STRAFING, TEXT("WASD Strafing"));
            AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT_CONTEXT_MENU_ITEM, TEXT("Exit StrafeHelper"));
            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_EXIT_CONTEXT_MENU_ITEM:
            PostQuitMessage(0);
            break;
        case ID_TRAY_WASD_STRAFING:
            isWASDStrafingEnabled = !isWASDStrafingEnabled;
            // If disabling, make sure to send key-ups and clear state.
            if (!isWASDStrafingEnabled) {
                for (int key : activeSpamKeys) {
                    SendKey(key, false);
                    KeyInfo[key].spamActive = false;
                }
                activeSpamKeys.clear();
                isCSpamActive = false;
                SetEvent(hSpamEvent);
            }
            break;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}
