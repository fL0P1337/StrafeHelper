#include <windows.h>
#include <shellapi.h>
#include <map>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iostream>

// Constants for tray icon and menu
#define ID_TRAY_APP_ICON                1001
#define ID_TRAY_EXIT_CONTEXT_MENU_ITEM  3000
#define ID_TRAY_WASD_STRAFING           3002
#define WM_TRAYICON                     (WM_USER + 1)

// Application details
const char* APP_NAME = "StrafeHelper";
const char* VERSION = "1.0_optimized";

// Configuration defaults (mutable, so they can be overwritten from config file)
int SPAM_DELAY_MS = 10;         // Delay between spamming cycles
int SPAM_KEY_DOWN_DURATION = 5; // Duration (in ms) keys remain down in a cycle

// Global configurable booleans
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
NOTIFYICONDATA nid;
std::map<int, KeyState> KeyInfo;   // Track state for keys: WASD
std::vector<int> activeSpamKeys;   // Currently active keys for strafing

// Event to signal spam thread for changes
HANDLE hSpamEvent = NULL;

// Forward declarations
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void InitNotifyIconData(HWND hwnd);
void SendKey(int targetKey, bool keyDown);
DWORD WINAPI SpamThread(LPVOID lpParam);
void loadConfig();

// Loads configuration from "config.cfg" file.
// Expected format: key=value pairs. Lines starting with '#' or empty lines are ignored.
void loadConfig() {
    std::ifstream configFile("config.cfg");
    if (!configFile.is_open()) {
        std::cout << "Config file not found. Using defaults." << std::endl;
        return;
    }

    std::cout << "Loading configuration from config.cfg..." << std::endl;
    std::string line;
    while (std::getline(configFile, line)) {
        // Trim off potential leading/trailing whitespace
        std::istringstream lineStream(line);
        std::string key;
        if (line.empty() || line[0] == '#')
            continue;

        size_t pos = line.find('=');
        if (pos == std::string::npos)
            continue; // Malformed line, skip

        key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        // Remove whitespace from key and value
        key.erase(remove_if(key.begin(), key.end(), isspace), key.end());
        value.erase(remove_if(value.begin(), value.end(), isspace), value.end());

        // Update timing configs and flags
        if (key == "SPAM_DELAY_MS")
            SPAM_DELAY_MS = std::stoi(value);
        else if (key == "SPAM_KEY_DOWN_DURATION")
            SPAM_KEY_DOWN_DURATION = std::stoi(value);
        else if (key == "isLocked")
            isLocked = (value == "true" || value == "1");
        else if (key == "isWASDStrafingEnabled")
            isWASDStrafingEnabled = (value == "true" || value == "1");
        // Update keybind customization
        else if (key == "KEY_SPAM_TRIGGER")
            KEY_SPAM_TRIGGER = static_cast<int>(value[0]);
    }
    configFile.close();

    // Output loaded configuration values
    std::cout << "Configuration loaded:" << std::endl;
    std::cout << "  SPAM_DELAY_MS: " << SPAM_DELAY_MS << std::endl;
    std::cout << "  SPAM_KEY_DOWN_DURATION: " << SPAM_KEY_DOWN_DURATION << std::endl;
    std::cout << "  isLocked: " << (isLocked ? "true" : "false") << std::endl;
    std::cout << "  isWASDStrafingEnabled: " << (isWASDStrafingEnabled ? "true" : "false") << std::endl;
    std::cout << "  KEY_SPAM_TRIGGER: " << static_cast<char>(KEY_SPAM_TRIGGER) << std::endl;
}

// Main function
int main() {
    // Allocate a console for output (for Windows subsystem applications)
    AllocConsole();
    SetConsoleTitleA(APP_NAME);

    // Load configuration from file
    loadConfig();

    // Create an event for the spam thread. Auto-reset.
    hSpamEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    // Register window class
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = TEXT("StrafeHelperClass");

    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, TEXT("Window Registration Failed!"), TEXT("Error"), MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }

    HWND hwnd = CreateWindowEx(0, wc.lpszClassName, TEXT("StrafeHelper"), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 240, 120, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) {
        MessageBox(NULL, TEXT("Window Creation Failed!"), TEXT("Error"), MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }

    // Initialize tray icon
    InitNotifyIconData(hwnd);

    // Initialize key states for WASD keys
    KeyInfo['W'] = KeyState();
    KeyInfo['A'] = KeyState();
    KeyInfo['S'] = KeyState();
    KeyInfo['D'] = KeyState();

    // Set the global keyboard hook
    hHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, NULL, 0);
    if (!hHook) {
        MessageBox(NULL, TEXT("Failed to install hook!"), TEXT("Error"), MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }

    // Start thread for spamming
    CreateThread(NULL, 0, SpamThread, NULL, 0, NULL);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Cleanup resources
    UnhookWindowsHookEx(hHook);
    Shell_NotifyIcon(NIM_DELETE, &nid);
    CloseHandle(hSpamEvent);

    return 0;
}

// Keyboard hook procedure
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        KBDLLHOOKSTRUCT* pKeybd = (KBDLLHOOKSTRUCT*)lParam;
        int keyCode = pKeybd->vkCode;

        // Update physical state for WASD keys
        if (keyCode == 'W' || keyCode == 'A' || keyCode == 'S' || keyCode == 'D') {
            if (wParam == WM_KEYDOWN) {
                KeyInfo[keyCode].physicalKeyDown = true;
            }
            else if (wParam == WM_KEYUP) {
                KeyInfo[keyCode].physicalKeyDown = false;
            }
        }

        // WASD Spam Logic using the custom spam trigger key
        if (isWASDStrafingEnabled) {
            if (keyCode == KEY_SPAM_TRIGGER) {
                if (wParam == WM_KEYDOWN) {
                    isCSpamActive = true;
                    // Check each WASD key and add to active list if physically down.
                    for (int key : { 'W', 'A', 'S', 'D' }) {
                        if (KeyInfo[key].physicalKeyDown &&
                            std::find(activeSpamKeys.begin(), activeSpamKeys.end(), key) == activeSpamKeys.end())
                        {
                            activeSpamKeys.push_back(key);
                            KeyInfo[key].spamActive = true;
                        }
                    }
                    // Signal the spam thread that state has changed.
                    SetEvent(hSpamEvent);
                }
                else if (wParam == WM_KEYUP) {
                    isCSpamActive = false;
                    // Ensure that any keys still active are explicitly released before clearing.
                    for (int key : activeSpamKeys) {
                        SendKey(key, false);
                    }
                    // Clear all pressed physical key states for WASD upon unholding the trigger
                    for (int key : { 'W', 'A', 'S', 'D' }) {
                        KeyInfo[key].physicalKeyDown = false;
                        KeyInfo[key].spamActive = false;
                    }
                    activeSpamKeys.clear();
                    SetEvent(hSpamEvent);
                }
                return CallNextHookEx(hHook, nCode, wParam, lParam);
            }

            // Block physical messages for WASD keys if we're spamming and not locked.
            if (!(pKeybd->flags & LLKHF_INJECTED) && !isLocked && isCSpamActive) {
                if (keyCode == 'W' || keyCode == 'A' || keyCode == 'S' || keyCode == 'D') {
                    if (wParam == WM_KEYDOWN) {
                        if (std::find(activeSpamKeys.begin(), activeSpamKeys.end(), keyCode) == activeSpamKeys.end()) {
                            activeSpamKeys.push_back(keyCode);
                            KeyInfo[keyCode].spamActive = true;
                            SetEvent(hSpamEvent);
                        }
                        return 1; // Block event
                    }
                    else if (wParam == WM_KEYUP) {
                        auto it = std::find(activeSpamKeys.begin(), activeSpamKeys.end(), keyCode);
                        if (it != activeSpamKeys.end()) {
                            SendKey(keyCode, false);
                            KeyInfo[keyCode].spamActive = false;
                            activeSpamKeys.erase(it);
                            SetEvent(hSpamEvent);
                        }
                        return 1; // Block event
                    }
                }
            }
        }
    }
    return CallNextHookEx(hHook, nCode, wParam, lParam);
}

// Spam thread: Sends grouped WASD key events for strafing.
DWORD WINAPI SpamThread(LPVOID lpParam) {
    INPUT inputs[16]; // Array to hold grouped INPUT records.
    while (true) {
        // Wait until state changes or timeout for next cycle.
        WaitForSingleObject(hSpamEvent, SPAM_DELAY_MS);

        if (isWASDStrafingEnabled && !activeSpamKeys.empty() && isCSpamActive) {
            // Capture the list of keys to spam at the start of the cycle
            std::vector<int> keysToSpam;
            for (int key : activeSpamKeys) {
                if (KeyInfo[key].spamActive) {
                    keysToSpam.push_back(key);
                }
            }
            if (!keysToSpam.empty()) {
                int count = 0;
                // Build array of key-down events for the captured keys
                for (int key : keysToSpam) {
                    inputs[count].type = INPUT_KEYBOARD;
                    inputs[count].ki.wVk = key;
                    inputs[count].ki.wScan = MapVirtualKey(key, 0);
                    inputs[count].ki.dwFlags = KEYEVENTF_SCANCODE;
                    count++;
                }
                if (count > 0)
                    SendInput(count, inputs, sizeof(INPUT));

                Sleep(SPAM_KEY_DOWN_DURATION);

                count = 0;
                // Build array of key-up events for the same captured keys
                for (int key : keysToSpam) {
                    inputs[count].type = INPUT_KEYBOARD;
                    inputs[count].ki.wVk = key;
                    inputs[count].ki.wScan = MapVirtualKey(key, 0);
                    inputs[count].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
                    count++;
                }
                if (count > 0)
                    SendInput(count, inputs, sizeof(INPUT));
            }
        }
    }
    return 0;
}

// Send a simulated key event.
void SendKey(int targetKey, bool keyDown) {
    INPUT input = { 0 };
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = targetKey;
    input.ki.wScan = MapVirtualKey(targetKey, 0);
    input.ki.dwFlags = KEYEVENTF_SCANCODE | (keyDown ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &input, sizeof(INPUT));
}

// Initialize the system tray icon.
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

// Window procedure for tray menu events.
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONDOWN) {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING | (isWASDStrafingEnabled ? MF_CHECKED : MF_UNCHECKED), ID_TRAY_WASD_STRAFING,
                TEXT("WASD Strafing"));
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