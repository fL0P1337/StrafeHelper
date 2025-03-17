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

// Constants for tray icon and menu
#define ID_TRAY_APP_ICON                1001
#define ID_TRAY_EXIT_CONTEXT_MENU_ITEM  3000
#define ID_TRAY_WASD_STRAFING           3002
#define WM_TRAYICON                     (WM_USER + 1)

// Application details
const char* APP_NAME = "StrafeHelper";
const char* VERSION = "2.0_performance";

// Configuration defaults (with atomic variables for thread safety)
std::atomic<int> SPAM_DELAY_MS{ 10 };         // Delay between spamming cycles
std::atomic<int> SPAM_KEY_DOWN_DURATION{ 5 }; // Duration (in ms) keys remain down in a cycle

// Global configurable atomics for thread safety without locks
std::atomic<bool> isLocked{ false };            // Global toggle: spam feature enabled/disabled
std::atomic<bool> isCSpamActive{ false };       // Spam trigger active flag
std::atomic<bool> isWASDStrafingEnabled{ true }; // Toggle for WASD Strafing

// Keybind customization
std::atomic<int> KEY_SPAM_TRIGGER{ 'C' };      // Default key to trigger WASD spamming

// Pre-allocated input buffers for faster response
INPUT keyDownInputs[4];  // Pre-allocated buffer for key down events (W,A,S,D)
INPUT keyUpInputs[4];    // Pre-allocated buffer for key up events (W,A,S,D)

// Structure to track key states with atomic flags
struct KeyState {
    std::atomic<bool> physicalKeyDown;  // Is the key physically pressed?
    std::atomic<bool> spamActive;       // Should the key be spammed?

    KeyState() : physicalKeyDown(false), spamActive(false) {}
};

// Global variables
HHOOK hHook = NULL;
NOTIFYICONDATA nid;
std::unordered_map<int, KeyState*> KeyInfo;   // Track state for keys: WASD
std::vector<int> activeSpamKeys;   // Currently active keys for strafing
std::mutex spamKeysMutex;          // Mutex to protect activeSpamKeys

// Event for thread signaling
HANDLE hSpamEvent = NULL;
// Event for terminating the thread cleanly
HANDLE hTerminateEvent = NULL;

// Thread handle for cleanup
HANDLE hSpamThreadHandle = NULL;

// Forward declarations
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void InitNotifyIconData(HWND hwnd);
void SendKey(int targetKey, bool keyDown);
DWORD WINAPI SpamThread(LPVOID lpParam);
void loadConfig();
void initializeInputArrays();
void cleanupResources();

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
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#')
            continue;

        size_t pos = line.find('=');
        if (pos == std::string::npos)
            continue; // Malformed line, skip

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        // Remove whitespace
        key.erase(remove_if(key.begin(), key.end(), isspace), key.end());
        value.erase(remove_if(value.begin(), value.end(), isspace), value.end());

        // Update configurations
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

// Initialize the pre-allocated input arrays for faster key event sending
void initializeInputArrays() {
    const int keys[4] = { 'W', 'A', 'S', 'D' };

    // Initialize key down inputs
    for (int i = 0; i < 4; i++) {
        keyDownInputs[i].type = INPUT_KEYBOARD;
        keyDownInputs[i].ki.wVk = keys[i];
        keyDownInputs[i].ki.wScan = MapVirtualKey(keys[i], 0);
        keyDownInputs[i].ki.dwFlags = KEYEVENTF_SCANCODE;
        keyDownInputs[i].ki.time = 0;
        keyDownInputs[i].ki.dwExtraInfo = 0;
    }

    // Initialize key up inputs
    for (int i = 0; i < 4; i++) {
        keyUpInputs[i].type = INPUT_KEYBOARD;
        keyUpInputs[i].ki.wVk = keys[i];
        keyUpInputs[i].ki.wScan = MapVirtualKey(keys[i], 0);
        keyUpInputs[i].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        keyUpInputs[i].ki.time = 0;
        keyUpInputs[i].ki.dwExtraInfo = 0;
    }
}

// Clean up all allocated resources
void cleanupResources() {
    // Clean up KeyInfo map
    for (auto& pair : KeyInfo) {
        delete pair.second;
    }
    KeyInfo.clear();

    // Clean up other resources
    if (hHook) UnhookWindowsHookEx(hHook);
    if (nid.hWnd) Shell_NotifyIcon(NIM_DELETE, &nid);
    if (hSpamEvent) CloseHandle(hSpamEvent);
    if (hTerminateEvent) CloseHandle(hTerminateEvent);
    if (hSpamThreadHandle) CloseHandle(hSpamThreadHandle);
}

// Main function
int main() {
    // Set high thread priority for better performance
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    // Allocate console for output
    AllocConsole();
    SetConsoleTitleA(APP_NAME);


    // Load configuration from file
    loadConfig();

    // Initialize pre-allocated input arrays
    initializeInputArrays();

    // Create events for thread synchronization
    hSpamEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    hTerminateEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    // Register window class with optimized settings
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = TEXT("StrafeHelperClass");
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, TEXT("Window Registration Failed!"), TEXT("Error"), MB_ICONEXCLAMATION | MB_OK);
        cleanupResources();
        return 1;
    }

    HWND hwnd = CreateWindowEx(0, wc.lpszClassName, TEXT("StrafeHelper"), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 240, 120, NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) {
        MessageBox(NULL, TEXT("Window Creation Failed!"), TEXT("Error"), MB_ICONEXCLAMATION | MB_OK);
        cleanupResources();
        return 1;
    }

    // Initialize tray icon
    InitNotifyIconData(hwnd);

    // Initialize key states for WASD keys using dynamic allocation
    KeyInfo['W'] = new KeyState();
    KeyInfo['A'] = new KeyState();
    KeyInfo['S'] = new KeyState();
    KeyInfo['D'] = new KeyState();

    // Set the global keyboard hook
    hHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, NULL, 0);
    if (!hHook) {
        MessageBox(NULL, TEXT("Failed to install hook!"), TEXT("Error"), MB_ICONEXCLAMATION | MB_OK);
        cleanupResources();
        return 1;
    }

    // Start thread for spamming with high priority
    hSpamThreadHandle = CreateThread(NULL, 0, SpamThread, NULL, 0, NULL);
    SetThreadPriority(hSpamThreadHandle, THREAD_PRIORITY_TIME_CRITICAL);

    std::cout << "StrafeHelper " << VERSION << " started successfully!" << std::endl;
    std::cout << "Press C + W/A/S/D for optimized strafing." << std::endl;

    // Message loop with prioritized handling
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // Signal thread termination
    SetEvent(hTerminateEvent);

    // Wait for thread to terminate (with timeout)
    WaitForSingleObject(hSpamThreadHandle, 1000);

    // Clean up resources
    cleanupResources();

    return 0;
}

// Keyboard hook procedure - optimized for minimal overhead
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
    if (keyCode != 'W' && keyCode != 'A' && keyCode != 'S' && keyCode != 'D' && keyCode != KEY_SPAM_TRIGGER) {
        return CallNextHookEx(hHook, nCode, wParam, lParam);
    }

    // Update physical state for WASD keys
    if (keyCode == 'W' || keyCode == 'A' || keyCode == 'S' || keyCode == 'D') {
        auto keyState = KeyInfo.find(keyCode);
        if (keyState != KeyInfo.end()) {
            if (isKeyDown) {
                keyState->second->physicalKeyDown = true;
            }
            else if (isKeyUp) {
                keyState->second->physicalKeyDown = false;
            }
        }
    }

    // WASD Spam Logic using the custom spam trigger key
    if (isWASDStrafingEnabled) {
        if (keyCode == KEY_SPAM_TRIGGER) {
            if (isKeyDown) {
                isCSpamActive = true;

                // Lock the mutex to safely modify shared data
                {
                    std::lock_guard<std::mutex> lock(spamKeysMutex);

                    // Check each WASD key and add to active list if physically down
                    for (int key : { 'W', 'A', 'S', 'D' }) {
                        auto keyState = KeyInfo.find(key);
                        if (keyState != KeyInfo.end() && keyState->second->physicalKeyDown &&
                            std::find(activeSpamKeys.begin(), activeSpamKeys.end(), key) == activeSpamKeys.end())
                        {
                            activeSpamKeys.push_back(key);
                            keyState->second->spamActive = true;
                        }
                    }
                }

                // Signal the spam thread that state has changed
                SetEvent(hSpamEvent);
            }
            else if (isKeyUp) {
                isCSpamActive = false;

                {
                    std::lock_guard<std::mutex> lock(spamKeysMutex);

                    // Only clear spamActive state for keys that were actually in activeSpamKeys
                    for (int key : activeSpamKeys) {
                        auto keyState = KeyInfo.find(key);
                        if (keyState != KeyInfo.end()) {
                            keyState->second->spamActive = false;
                        }

                        // Send key up event only for keys that were being spammed
                        INPUT input = {};
                        input.type = INPUT_KEYBOARD;
                        input.ki.wVk = key;
                        input.ki.dwFlags = KEYEVENTF_KEYUP;
                        SendInput(1, &input, sizeof(INPUT));
                    }
                    activeSpamKeys.clear();
                }

                // Signal the spam thread
                SetEvent(hSpamEvent);
            }
            return CallNextHookEx(hHook, nCode, wParam, lParam);
        }

        // Handle WASD key events when spamming is active
        if (!isInjected && !isLocked && isCSpamActive) {
            if (keyCode == 'W' || keyCode == 'A' || keyCode == 'S' || keyCode == 'D') {
                {
                    std::lock_guard<std::mutex> lock(spamKeysMutex);

                    if (isKeyDown) {
                        if (std::find(activeSpamKeys.begin(), activeSpamKeys.end(), keyCode) == activeSpamKeys.end()) {
                            activeSpamKeys.push_back(keyCode);
                            auto keyState = KeyInfo.find(keyCode);
                            if (keyState != KeyInfo.end()) {
                                keyState->second->spamActive = true;
                            }
                            SetEvent(hSpamEvent);
                        }
                        return 1; // Block event
                    }
                    else if (isKeyUp) {
                        auto it = std::find(activeSpamKeys.begin(), activeSpamKeys.end(), keyCode);
                        if (it != activeSpamKeys.end()) {
                            auto keyState = KeyInfo.find(keyCode);
                            if (keyState != KeyInfo.end()) {
                                keyState->second->spamActive = false;
                            }
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

// Optimized spam thread with minimal latency
DWORD WINAPI SpamThread(LPVOID lpParam) {
    // Set thread affinity to a single core for more consistent timing
    SetThreadAffinityMask(GetCurrentThread(), 1);

    // Variables for the high-resolution performance counter
    LARGE_INTEGER freq, startTime, endTime, elapsedMicroseconds;
    QueryPerformanceFrequency(&freq);

    // For holding active keys during each cycle
    std::vector<int> keysToSpam;
    HANDLE events[2] = { hSpamEvent, hTerminateEvent };

    while (true) {
        // Wait for event signal or timeout
        DWORD waitResult = WaitForMultipleObjects(2, events, FALSE, SPAM_DELAY_MS);

        // Check if termination was requested
        if (waitResult == WAIT_OBJECT_0 + 1) {
            break;
        }

        if (isWASDStrafingEnabled && isCSpamActive) {
            QueryPerformanceCounter(&startTime);

            // Safely copy the keys to spam
            {
                std::lock_guard<std::mutex> lock(spamKeysMutex);
                keysToSpam.clear();
                for (int key : activeSpamKeys) {
                    auto keyState = KeyInfo.find(key);
                    if (keyState != KeyInfo.end() && keyState->second->spamActive) {
                        keysToSpam.push_back(key);
                    }
                }
            }

            if (!keysToSpam.empty()) {
                // Find which pre-allocated input structures to use and how many
                int keyDownCount = 0;
                int keyIndices[4] = { -1, -1, -1, -1 }; // Maps keys to their indices in the array

                for (int key : keysToSpam) {
                    switch (key) {
                    case 'W': keyIndices[keyDownCount++] = 0; break;
                    case 'A': keyIndices[keyDownCount++] = 1; break;
                    case 'S': keyIndices[keyDownCount++] = 2; break;
                    case 'D': keyIndices[keyDownCount++] = 3; break;
                    }
                }

                // Prepare arrays for this specific key combination
                INPUT* activeKeyDowns = new INPUT[keyDownCount];
                INPUT* activeKeyUps = new INPUT[keyDownCount];

                for (int i = 0; i < keyDownCount; i++) {
                    activeKeyDowns[i] = keyDownInputs[keyIndices[i]];
                    activeKeyUps[i] = keyUpInputs[keyIndices[i]];
                }

                // Send key down events as a batch
                if (keyDownCount > 0) {
                    SendInput(keyDownCount, activeKeyDowns, sizeof(INPUT));
                }

                // Sleep for key down duration using high precision timer if possible
                int keyDownDuration = SPAM_KEY_DOWN_DURATION;
                if (keyDownDuration <= 1) {
                    // For extremely low durations, use spinwait instead of Sleep
                    LARGE_INTEGER targetTime;
                    QueryPerformanceCounter(&targetTime);
                    targetTime.QuadPart += freq.QuadPart * keyDownDuration / 1000;

                    LARGE_INTEGER currentTime;
                    do {
                        QueryPerformanceCounter(&currentTime);
                    } while (currentTime.QuadPart < targetTime.QuadPart);
                }
                else {
                    Sleep(keyDownDuration);
                }

                // Send key up events as a batch
                if (keyDownCount > 0) {
                    SendInput(keyDownCount, activeKeyUps, sizeof(INPUT));
                }

                // Clean up
                delete[] activeKeyDowns;
                delete[] activeKeyUps;
            }

            // Calculate how long this cycle took for debugging/optimization
            QueryPerformanceCounter(&endTime);
            elapsedMicroseconds.QuadPart = endTime.QuadPart - startTime.QuadPart;
            elapsedMicroseconds.QuadPart *= 1000000;
            elapsedMicroseconds.QuadPart /= freq.QuadPart;

            // Uncomment for performance debugging
            // std::cout << "Cycle execution time: " << elapsedMicroseconds.QuadPart << " microseconds\n";
        }
    }
    return 0;
}

// Send a simulated key event with minimal overhead
void SendKey(int targetKey, bool keyDown) {
    INPUT input = { 0 };
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = targetKey;
    input.ki.wScan = MapVirtualKey(targetKey, 0);
    input.ki.dwFlags = KEYEVENTF_SCANCODE | (keyDown ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &input, sizeof(INPUT));
}

// Initialize the system tray icon with optimized settings
void InitNotifyIconData(HWND hwnd) {
    ZeroMemory(&nid, sizeof(NOTIFYICONDATA));
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = ID_TRAY_APP_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    lstrcpy(nid.szTip, TEXT("StrafeHelper - Optimized Performance"));
    Shell_NotifyIcon(NIM_ADD, &nid);
}

// Window procedure for tray menu events - optimized to avoid menu latency
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HMENU hMenu = NULL;

    switch (msg) {
    case WM_CREATE:
        // Pre-create menu for faster response
        hMenu = CreatePopupMenu();
        AppendMenu(hMenu, MF_STRING, ID_TRAY_WASD_STRAFING, TEXT("WASD Strafing"));
        AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
        AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT_CONTEXT_MENU_ITEM, TEXT("Exit StrafeHelper"));
        break;

    case WM_TRAYICON:
        if (lParam == WM_RBUTTONDOWN) {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);

            // Update menu check state
            CheckMenuItem(hMenu, ID_TRAY_WASD_STRAFING,
                (isWASDStrafingEnabled ? MF_CHECKED : MF_UNCHECKED));

            // Show menu
            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_EXIT_CONTEXT_MENU_ITEM:
            DestroyWindow(hwnd); // Will trigger WM_DESTROY
            break;

        case ID_TRAY_WASD_STRAFING:
            isWASDStrafingEnabled = !isWASDStrafingEnabled;
            if (!isWASDStrafingEnabled) {
                std::lock_guard<std::mutex> lock(spamKeysMutex);
                for (int key : activeSpamKeys) {
                    auto keyState = KeyInfo.find(key);
                    if (keyState != KeyInfo.end()) {
                        keyState->second->spamActive = false;
                    }
                }
                activeSpamKeys.clear();
                isCSpamActive = false;
                SetEvent(hSpamEvent);
            }
            // Update menu check state
            CheckMenuItem(hMenu, ID_TRAY_WASD_STRAFING,
                (isWASDStrafingEnabled ? MF_CHECKED : MF_UNCHECKED));
            break;
        }
        break;

    case WM_DESTROY:
        if (hMenu) DestroyMenu(hMenu);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}