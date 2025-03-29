#define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
#include <windows.h>
#include <shellapi.h>     // For Tray Icon
#include <map>
#include <vector>
#include <atomic>
#include <thread>         // Consider std::thread for RAII (optional)
#include <mutex>          // Consider std::mutex (optional, CRITICAL_SECTION is often faster in Windows)
#include <chrono>         // For timing
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iostream>
#include <tchar.h>        // For TEXT macro etc.
#include <strsafe.h>      // For StringCchPrintf etc.

// --- Configuration and Constants ---

namespace Config {
    // Application Info - Use const char* for std::cout/cerr compatibility
    const char APP_NAME[] = "StrafeHelper";
    const char VERSION[] = "1.2_optimized_fix2"; // Updated version
    const char CONFIG_FILE_NAME[] = "config.cfg"; // Use char* for std::ifstream

    // Use TCHAR for Windows API strings
    const TCHAR WINDOW_CLASS_NAME[] = TEXT("StrafeHelperWindowClass");
    const TCHAR WINDOW_TITLE[] = TEXT("StrafeHelper Hidden Window");

    // Default Settings (can be overridden by config file)
    std::atomic<int> SpamDelayMs = 10;
    std::atomic<int> SpamKeyDownDurationMs = 5;
    std::atomic<bool> IsLocked = false; // Feature toggle (currently unused in core logic)
    std::atomic<bool> IsWASDStrafingEnabled = true; // Master switch for the feature
    std::atomic<int> KeySpamTrigger = 'C'; // VK Code for the trigger key
} // namespace Config

namespace Globals {
    // --- WinAPI Handles ---
    HHOOK g_hHook = NULL;
    HWND g_hWindow = NULL; // Handle to the hidden message window
    HANDLE g_hSpamThread = NULL;
    HANDLE g_hSpamEvent = NULL; // Auto-reset event to signal/wake spam thread
    HINSTANCE g_hInstance = NULL; // Store instance handle

    // --- State Management ---
    struct KeyState {
        std::atomic<bool> physicalKeyDown = false;
        std::atomic<bool> spamming = false;
        // Consider caching scan code if MapVirtualKey proves to be a bottleneck
        // WORD scanCode = 0;

        // Default constructor needed for map insertion via operator[]
        KeyState() : physicalKeyDown(false), spamming(false) {}
    };

    // Map VK code to its state. Only contains keys we actively track (WASD + Trigger).
    std::map<int, KeyState> g_KeyInfo;

    // List of VK codes currently being actively spammed. Protected by g_csActiveKeys.
    std::vector<int> g_activeSpamKeys;
    CRITICAL_SECTION g_csActiveKeys; // Protects g_activeSpamKeys

    // Flag indicating if the spamming mechanism is currently active (Trigger key held down).
    std::atomic<bool> g_isCSpamActive = false;

    // --- Tray Icon ---
    NOTIFYICONDATA g_nid = { 0 }; // Tray icon data structure
    const UINT WM_TRAYICON = WM_APP + 1; // Custom message for tray icon events
    const UINT ID_TRAY_APP_ICON = 1001;
    const UINT ID_TRAY_EXIT_MENU_ITEM = 3000;
    const UINT ID_TRAY_TOGGLE_STRAFING_ITEM = 3002;

} // namespace Globals

// --- Forward Declarations ---
// Core Logic
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
DWORD WINAPI SpamThread(LPVOID lpParam);
void SendKeyInputBatch(const std::vector<int>& keys, bool keyDown);
void CleanupSpamState(bool restoreHeldKeys);

// Setup & Teardown
bool InitializeApplication(HINSTANCE hInstance);
void CleanupApplication();
bool SetupHooksAndThreads();
void TeardownHooksAndThreads();
bool CreateAppWindow(HINSTANCE hInstance);
void LoadConfig();
void LogError(const std::string& message, DWORD errorCode = 0);

// Window & Tray
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void InitNotifyIconData(HWND hwnd);
void ShowContextMenu(HWND hwnd);


// --- Main Function ---
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    Globals::g_hInstance = hInstance;

#ifdef _DEBUG
    // Allocate console only if not already present (e.g., launched from cmd)
    if (!GetConsoleWindow()) {
        AllocConsole();
        FILE* pCout, * pCerr;
        freopen_s(&pCout, "CONOUT$", "w", stdout);
        freopen_s(&pCerr, "CONOUT$", "w", stderr);
        // Use TEXT macro for the Windows API call SetConsoleTitle
        SetConsoleTitle(TEXT("StrafeHelper Debug Console"));
    }
#endif

    // Use const char* directly with std::cout
    std::cout << Config::APP_NAME << " v" << Config::VERSION << " starting..." << std::endl;

    if (!InitializeApplication(hInstance)) {
        LogError("Application Initialization Failed!");
        CleanupApplication(); // Attempt partial cleanup
        return 1;
    }

    if (!SetupHooksAndThreads()) {
        LogError("Failed to setup hooks or threads!");
        CleanupApplication();
        return 1;
    }

    std::cout << "StrafeHelper running. Right-click tray icon to configure or exit." << std::endl;

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    std::cout << "Exiting StrafeHelper..." << std::endl;

    // Normal cleanup is triggered by WM_DESTROY calling PostQuitMessage
    // TeardownHooksAndThreads(); // Usually done in WM_DESTROY handler
    // CleanupApplication();     // Usually done in WM_DESTROY handler

    return (int)msg.wParam;
}

// --- Initialization and Cleanup ---

bool InitializeApplication(HINSTANCE hInstance) {
    // Initialize critical section early
    InitializeCriticalSection(&Globals::g_csActiveKeys);

    LoadConfig(); // Load settings from file

    // Create the auto-reset event
    Globals::g_hSpamEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (!Globals::g_hSpamEvent) {
        LogError("CreateEvent failed", GetLastError());
        return false;
    }

    if (!CreateAppWindow(hInstance)) {
        return false; // Error already logged
    }

    // Initialize key states using operator[] for default construction
    Globals::g_KeyInfo['W']; Globals::g_KeyInfo['A'];
    Globals::g_KeyInfo['S']; Globals::g_KeyInfo['D'];
    if (Globals::g_KeyInfo.find(Config::KeySpamTrigger) == Globals::g_KeyInfo.end()) {
        Globals::g_KeyInfo[Config::KeySpamTrigger]; // Ensure trigger key state exists
    }

    InitNotifyIconData(Globals::g_hWindow); // Setup tray icon

    return true;
}

void CleanupApplication() {
    std::cout << "Running CleanupApplication..." << std::endl;

    // Ensure hooks and threads are stopped *before* destroying window/event/cs
    TeardownHooksAndThreads();

    // Remove tray icon
    if (Globals::g_nid.hWnd) {
        Shell_NotifyIcon(NIM_DELETE, &Globals::g_nid);
        // Destroy the icon handle if loaded manually (LoadImage with LR_SHARED usually doesn't need explicit destroy)
        // if (Globals::g_nid.hIcon) DestroyIcon(Globals::g_nid.hIcon);
        Globals::g_nid.hWnd = NULL; // Mark as removed
    }

    // Destroy the hidden window
    if (Globals::g_hWindow) {
        DestroyWindow(Globals::g_hWindow);
        Globals::g_hWindow = NULL;
    }

    // Unregister window class
    if (Globals::g_hInstance) {
        UnregisterClass(Config::WINDOW_CLASS_NAME, Globals::g_hInstance);
    }

    // Close event handle
    if (Globals::g_hSpamEvent) {
        CloseHandle(Globals::g_hSpamEvent);
        Globals::g_hSpamEvent = NULL;
    }

    // Delete critical section
    // Note: Only delete if successfully initialized. Add a flag if needed, but usually safe here.
    DeleteCriticalSection(&Globals::g_csActiveKeys);

#ifdef _DEBUG
    // Release console if we allocated it
    // Be careful if launched from an existing console
    // FreeConsole(); // This might close the parent console too, use with caution.
#endif
    std::cout << "CleanupApplication finished." << std::endl;
}

bool SetupHooksAndThreads() {
    // Set the global keyboard hook
    Globals::g_hHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, Globals::g_hInstance, 0);
    if (!Globals::g_hHook) {
        LogError("SetWindowsHookEx failed", GetLastError());
        return false;
    }

    // Start the spam thread
    Globals::g_hSpamThread = CreateThread(NULL, 0, SpamThread, NULL, 0, NULL);
    if (!Globals::g_hSpamThread) {
        LogError("CreateThread failed", GetLastError());
        UnhookWindowsHookEx(Globals::g_hHook); // Clean up hook if thread fails
        Globals::g_hHook = NULL;
        return false;
    }

    return true;
}

void TeardownHooksAndThreads() {
    std::cout << "Running TeardownHooksAndThreads..." << std::endl;
    // Unhook keyboard listener
    if (Globals::g_hHook) {
        UnhookWindowsHookEx(Globals::g_hHook);
        Globals::g_hHook = NULL;
    }

    // Signal spam thread to potentially exit (add a dedicated exit flag for robustness)
    // and wait briefly for it (optional, prevents errors if thread accesses deleted resources)
    if (Globals::g_hSpamEvent) {
        SetEvent(Globals::g_hSpamEvent); // Wake it up
    }
    if (Globals::g_hSpamThread) {
        // A more robust exit uses an atomic flag checked in the thread loop
        // WaitForSingleObject(Globals::g_hSpamThread, 500); // Wait max 0.5 sec
        CloseHandle(Globals::g_hSpamThread);
        Globals::g_hSpamThread = NULL;
    }
    std::cout << "TeardownHooksAndThreads finished." << std::endl;
}

bool CreateAppWindow(HINSTANCE hInstance) {
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = Config::WINDOW_CLASS_NAME; // TCHAR required here
    wc.hIcon = (HICON)LoadImage(hInstance, IDI_APPLICATION, IMAGE_ICON, 0, 0, LR_SHARED);
    wc.hIconSm = (HICON)LoadImage(hInstance, IDI_APPLICATION, IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassEx(&wc)) {
        LogError("RegisterClassEx failed", GetLastError());
        return false;
    }

    // Create a minimal, hidden message-only window
    Globals::g_hWindow = CreateWindowEx(
        WS_EX_TOOLWINDOW, // Hide from taskbar and alt-tab
        Config::WINDOW_CLASS_NAME, // TCHAR required here
        Config::WINDOW_TITLE,      // TCHAR required here
        WS_POPUP, // No border, title bar, etc.
        0, 0, 0, 0, // Position and size don't matter
        HWND_MESSAGE, // Optimize as message-only window (optional)
        NULL, hInstance, NULL);

    if (!Globals::g_hWindow) {
        LogError("CreateWindowEx failed", GetLastError());
        UnregisterClass(Config::WINDOW_CLASS_NAME, hInstance); // Clean up class registration
        return false;
    }

    return true;
}

// --- Core Logic Implementations ---

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // 1. Initial checks - exit early if possible
    if (nCode != HC_ACTION) {
        return CallNextHookEx(Globals::g_hHook, nCode, wParam, lParam);
    }

    KBDLLHOOKSTRUCT* pKeybd = (KBDLLHOOKSTRUCT*)lParam;
    int vkCode = pKeybd->vkCode;
    bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    bool isKeyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP); // Explicitly check keyup

    // Ignore injected events to prevent loops with SendInput
    // Consider setting dwExtraInfo in SendInput for more robust filtering if needed
    if (pKeybd->flags & LLKHF_INJECTED) {
        return CallNextHookEx(Globals::g_hHook, nCode, wParam, lParam);
    }

    // 2. Check if the key is one we potentially manage (WASD or Trigger)
    auto itKeyInfo = Globals::g_KeyInfo.find(vkCode);
    bool isManagedKey = (itKeyInfo != Globals::g_KeyInfo.end());
    bool isWASD = (vkCode == 'W' || vkCode == 'A' || vkCode == 'S' || vkCode == 'D');
    // Load atomic trigger key config value
    int currentTriggerKey = Config::KeySpamTrigger.load(std::memory_order_relaxed);
    bool isTrigger = (vkCode == currentTriggerKey);


    // 3. Update physical key state if managed
    if (isManagedKey) {
        // Use relaxed memory order for simple flag updates
        itKeyInfo->second.physicalKeyDown.store(isKeyDown, std::memory_order_relaxed);
    }

    // 4. Handle Trigger Key Logic
    // Load atomic enabled flag
    if (isTrigger && Config::IsWASDStrafingEnabled.load(std::memory_order_relaxed)) {
        if (isKeyDown) {
            // Activate spam only if it wasn't already active (debouncing effect)
            if (!Globals::g_isCSpamActive.load(std::memory_order_relaxed)) {
                Globals::g_isCSpamActive.store(true, std::memory_order_relaxed);
                std::cout << "Spam Activated" << std::endl;

                std::vector<int> keysToStartSpamming;
                // Check WASD physical state *without* locking KeyInfo (atomics allow this)
                for (int key : {'W', 'A', 'S', 'D'}) {
                    // Get a non-const reference to modify atomics
                    Globals::KeyState& keyStateRef = Globals::g_KeyInfo[key]; // Non-const ref
                    // Use the reference to check physicalKeyDown and store spamming state
                    if (keyStateRef.physicalKeyDown.load(std::memory_order_relaxed)) {
                        keyStateRef.spamming.store(true, std::memory_order_relaxed); // Use non-const ref
                        keysToStartSpamming.push_back(key);
                        // Send immediate UP to counteract the physical down event
                        INPUT input = { INPUT_KEYBOARD };
                        input.ki.wVk = key;
                        input.ki.wScan = MapVirtualKey(key, MAPVK_VK_TO_VSC);
                        input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
                        input.ki.dwExtraInfo = GetMessageExtraInfo();
                        SendInput(1, &input, sizeof(INPUT));
                    }
                    else {
                        keyStateRef.spamming.store(false, std::memory_order_relaxed); // Use non-const ref
                    }
                }

                // Update shared activeSpamKeys list (requires lock)
                EnterCriticalSection(&Globals::g_csActiveKeys);
                Globals::g_activeSpamKeys = keysToStartSpamming; // Replace content
                LeaveCriticalSection(&Globals::g_csActiveKeys);

                SetEvent(Globals::g_hSpamEvent); // Wake spam thread
            }
            // Allow trigger keydown to pass through (e.g., typing 'C')
            // return 1; // Uncomment to block 'C' itself when used as trigger
        }
        else if (isKeyUp) { // Trigger key released
            if (Globals::g_isCSpamActive.load(std::memory_order_relaxed)) {
                std::cout << "Spam Deactivated" << std::endl;
                CleanupSpamState(true); // Cleanup and restore held keys
            }
            // Allow trigger keyup to pass through
        }
        // If trigger key, let it pass through unless specifically blocked above
        return CallNextHookEx(Globals::g_hHook, nCode, wParam, lParam);
    } // End Trigger Key Logic

    // 5. Handle WASD Key Logic (only if spam is currently active)
    // Load atomic enabled/active flags
    if (isWASD && Config::IsWASDStrafingEnabled.load(std::memory_order_relaxed) && Globals::g_isCSpamActive.load(std::memory_order_relaxed)) {
        // Use full type name Globals::KeyState&
        Globals::KeyState& keyState = itKeyInfo->second; // Reference to the key's state

        if (isKeyDown) {
            // If key wasn't already marked for spamming (e.g., pressed *after* C)
            if (!keyState.spamming.load(std::memory_order_relaxed)) { // Use keyState
                keyState.spamming.store(true, std::memory_order_relaxed); // Use keyState

                // Add to shared list (requires lock)
                EnterCriticalSection(&Globals::g_csActiveKeys);
                // Avoid duplicates if logic allows re-pressing
                if (std::find(Globals::g_activeSpamKeys.begin(), Globals::g_activeSpamKeys.end(), vkCode) == Globals::g_activeSpamKeys.end()) {
                    Globals::g_activeSpamKeys.push_back(vkCode);
                }
                LeaveCriticalSection(&Globals::g_csActiveKeys);

                // Send immediate UP to counteract physical down
                INPUT input = { INPUT_KEYBOARD };
                input.ki.wVk = vkCode;
                input.ki.wScan = MapVirtualKey(vkCode, MAPVK_VK_TO_VSC);
                input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
                input.ki.dwExtraInfo = GetMessageExtraInfo();
                SendInput(1, &input, sizeof(INPUT));

                SetEvent(Globals::g_hSpamEvent); // Wake spam thread
            }
            // Block original WASD keydown when spam is active
            return 1;
        }
        else if (isKeyUp) { // WASD key released while spam active
            if (keyState.spamming.load(std::memory_order_relaxed)) { // Use keyState
                keyState.spamming.store(false, std::memory_order_relaxed); // Use keyState

                // Remove from shared list (requires lock)
                EnterCriticalSection(&Globals::g_csActiveKeys);
                Globals::g_activeSpamKeys.erase(
                    std::remove(Globals::g_activeSpamKeys.begin(), Globals::g_activeSpamKeys.end(), vkCode),
                    Globals::g_activeSpamKeys.end()
                );
                LeaveCriticalSection(&Globals::g_csActiveKeys);

                // Send final UP to ensure game sees it as released
                INPUT input = { INPUT_KEYBOARD };
                input.ki.wVk = vkCode;
                input.ki.wScan = MapVirtualKey(vkCode, MAPVK_VK_TO_VSC);
                input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
                input.ki.dwExtraInfo = GetMessageExtraInfo();
                SendInput(1, &input, sizeof(INPUT));

                SetEvent(Globals::g_hSpamEvent); // Wake spam thread
            }
            // Block original WASD keyup when spam is active
            return 1;
        }
    } // End WASD Key Logic (when spam active)

    // 6. If event wasn't handled/blocked above, pass it on
    return CallNextHookEx(Globals::g_hHook, nCode, wParam, lParam);
}


// Cleans up the spamming state. Called when trigger is released or feature disabled.
void CleanupSpamState(bool restoreHeldKeys) {
    // Ensure C spam active flag is false
    Globals::g_isCSpamActive.store(false, std::memory_order_relaxed);

    std::vector<int> keysThatWereSpamming;
    std::vector<int> keysToRestoreDown;

    // --- Critical Section Start ---
    EnterCriticalSection(&Globals::g_csActiveKeys);
    keysThatWereSpamming = Globals::g_activeSpamKeys; // Copy the list
    Globals::g_activeSpamKeys.clear(); // Clear the shared list
    LeaveCriticalSection(&Globals::g_csActiveKeys);
    // --- Critical Section End ---

    // Reset spamming flags and determine which keys need restoring
    for (int key : {'W', 'A', 'S', 'D'}) {
        // Declare 'state' inside the loop with correct type
        Globals::KeyState& state = Globals::g_KeyInfo[key]; // Assumes key exists
        state.spamming.store(false, std::memory_order_relaxed); // Ensure spamming flag is off
        // Check if the key *is still physically held down* AFTER spam stopped
        if (restoreHeldKeys && state.physicalKeyDown.load(std::memory_order_relaxed)) {
            keysToRestoreDown.push_back(key);
        }
    }

    // 1. Send final KEY UP for all keys that were being spammed
    if (!keysThatWereSpamming.empty()) {
        // Use helper for bulk send
        SendKeyInputBatch(keysThatWereSpamming, false); // false = key up
        std::cout << "  Sent final UP for spammed keys." << std::endl;
    }

    // Give OS/App a moment to process the key ups (Adjust timing if needed)
    if (restoreHeldKeys && !keysToRestoreDown.empty()) {
        Sleep(5); // Small delay only if restoring keys afterwards
    }

    // 2. Send KEY DOWN for keys that are still physically held
    if (restoreHeldKeys && !keysToRestoreDown.empty()) {
        SendKeyInputBatch(keysToRestoreDown, true); // true = key down
        std::cout << "  Restored DOWN state for physically held keys: ";
        for (int k : keysToRestoreDown) std::cout << static_cast<char>(k) << " ";
        std::cout << std::endl;
    }

    // Signal spam thread (it will check g_isCSpamActive and stop spamming)
    SetEvent(Globals::g_hSpamEvent);
}


// Sends a batch of key events (all UP or all DOWN) using SendInput
void SendKeyInputBatch(const std::vector<int>& keys, bool keyDown) {
    if (keys.empty()) return;

    std::vector<INPUT> inputs(keys.size());
    for (size_t i = 0; i < keys.size(); ++i) {
        inputs[i].type = INPUT_KEYBOARD;
        inputs[i].ki.wVk = keys[i];
        inputs[i].ki.wScan = MapVirtualKey(keys[i], MAPVK_VK_TO_VSC);
        inputs[i].ki.dwFlags = KEYEVENTF_SCANCODE | (keyDown ? 0 : KEYEVENTF_KEYUP);
        inputs[i].ki.time = 0;
        // Consider unique dwExtraInfo if needed for advanced injected event filtering
        inputs[i].ki.dwExtraInfo = GetMessageExtraInfo();
    }
    SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

DWORD WINAPI SpamThread(LPVOID lpParam) {
    // Pre-allocate buffer, slightly larger than max needed (4 keys)
    std::vector<int> localActiveKeys;
    localActiveKeys.reserve(8); // Reserve space

    // Use high-resolution timer potentially for sleep duration? Optional.
    // timeBeginPeriod(1); // Request 1ms timer resolution (system-wide effect!)

    while (true) {
        DWORD spamDelay = Config::SpamDelayMs.load(std::memory_order_relaxed);
        DWORD waitTimeout = INFINITE; // Default: Wait forever

        // Only set a timeout if spam is active AND enabled
        if (Globals::g_isCSpamActive.load(std::memory_order_relaxed) &&
            Config::IsWASDStrafingEnabled.load(std::memory_order_relaxed))
        {
            // --- Critical Section Start (Briefly check if keys are active) ---
            EnterCriticalSection(&Globals::g_csActiveKeys);
            bool keysCurrentlyActive = !Globals::g_activeSpamKeys.empty();
            LeaveCriticalSection(&Globals::g_csActiveKeys);
            // --- Critical Section End ---

            if (keysCurrentlyActive) {
                waitTimeout = spamDelay; // Use configured delay if keys are pressed
            }
        }

        // Wait for signal or timeout
        DWORD waitResult = WaitForSingleObject(Globals::g_hSpamEvent, waitTimeout);

        // --- After Wait ---

        // Check if we should stop spamming (feature disabled or trigger released)
        // Check *after* the wait, as state could change during wait.
        if (!Globals::g_isCSpamActive.load(std::memory_order_relaxed) ||
            !Config::IsWASDStrafingEnabled.load(std::memory_order_relaxed))
        {
            continue; // Go back to top and wait indefinitely or until re-enabled
        }

        // Get a local copy of keys to spam for this cycle
        // --- Critical Section Start ---
        EnterCriticalSection(&Globals::g_csActiveKeys);
        localActiveKeys = Globals::g_activeSpamKeys; // Copy the vector
        LeaveCriticalSection(&Globals::g_csActiveKeys);
        // --- Critical Section End ---

        if (localActiveKeys.empty()) {
            continue; // No keys active right now, wait for signal
        }

        // --- Release Phase ---
        SendKeyInputBatch(localActiveKeys, false); // false = Key UP

        // --- Wait (Key Down Duration) ---
        DWORD keyDownDuration = Config::SpamKeyDownDurationMs.load(std::memory_order_relaxed);
        if (keyDownDuration > 0) {
            Sleep(keyDownDuration); // Use Sleep for simplicity, consider alternatives if precision is critical
        }

        // Re-check state *after* sleep, before sending key down
        if (!Globals::g_isCSpamActive.load(std::memory_order_relaxed) ||
            !Config::IsWASDStrafingEnabled.load(std::memory_order_relaxed))
        {
            // If state changed during sleep, don't send the key down, just loop
            // The CleanupSpamState should have sent a final key UP already.
            continue;
        }

        // It's possible the active keys changed during sleep. Re-acquire the list?
        // Optimization: Assume keys rarely change *exactly* during the short sleep.
        // Using the `localActiveKeys` from before sleep is usually fine and avoids extra lock.
        // If issues arise, re-acquire the list here:
        // EnterCriticalSection(&Globals::g_csActiveKeys);
        // localActiveKeys = Globals::g_activeSpamKeys;
        // LeaveCriticalSection(&Globals::g_csActiveKeys);
        // if (localActiveKeys.empty()) continue;

        // --- Press Phase ---
        // Only press keys that are still in the active list (re-acquire for safety before press)
        EnterCriticalSection(&Globals::g_csActiveKeys);
        localActiveKeys = Globals::g_activeSpamKeys; // Update local copy before pressing
        LeaveCriticalSection(&Globals::g_csActiveKeys);

        if (!localActiveKeys.empty()) {
            SendKeyInputBatch(localActiveKeys, true); // true = Key DOWN
        }


        // The main delay is handled by the WaitForSingleObject timeout at the loop start
    }

    // timeEndPeriod(1); // Release high-resolution timer if used
    return 0;
}


// --- Configuration Loading ---

void LoadConfig() {
    // Use const char* file name with std::ifstream
    std::ifstream configFile(Config::CONFIG_FILE_NAME);
    if (!configFile.is_open()) {
        // Use const char* directly with std::cout/cerr
        std::cout << "Config file '" << Config::CONFIG_FILE_NAME << "' not found. Using defaults." << std::endl;
        return;
    }

    std::cout << "Loading configuration from " << Config::CONFIG_FILE_NAME << "..." << std::endl;
    std::string line;
    int lineNumber = 0;
    while (std::getline(configFile, line)) {
        lineNumber++;
        std::string trimmedLine;
        // Trim leading/trailing whitespace and ignore comments/empty lines
        size_t first = line.find_first_not_of(" \t\n\r\f\v");
        if (first == std::string::npos || line[first] == '#') {
            continue;
        }
        size_t last = line.find_last_not_of(" \t\n\r\f\v");
        trimmedLine = line.substr(first, (last - first + 1));

        size_t equalsPos = trimmedLine.find('=');
        if (equalsPos == std::string::npos) {
            std::cerr << "Warning: Malformed line " << lineNumber << " in config: " << line << std::endl;
            continue;
        }

        std::string key = trimmedLine.substr(0, equalsPos);
        std::string value = trimmedLine.substr(equalsPos + 1);
        // Trim key/value again
        key.erase(key.find_last_not_of(" \t\n\r\f\v") + 1);
        value.erase(0, value.find_first_not_of(" \t\n\r\f\v"));


        try {
            if (key == "SPAM_DELAY_MS")
                Config::SpamDelayMs = std::stoi(value);
            else if (key == "SPAM_KEY_DOWN_DURATION")
                Config::SpamKeyDownDurationMs = std::stoi(value);
            else if (key == "isLocked")
                Config::IsLocked = (value == "true" || value == "1");
            else if (key == "isWASDStrafingEnabled")
                Config::IsWASDStrafingEnabled = (value == "true" || value == "1");
            else if (key == "KEY_SPAM_TRIGGER") {
                if (!value.empty()) {
                    // Use VkKeyScan to get VK code if possible (handles shift states etc.)
                    SHORT vkScanResult = VkKeyScanA(value[0]);
                    if (vkScanResult != -1) {
                        Config::KeySpamTrigger = LOBYTE(vkScanResult);
                    }
                    else {
                        // Fallback: Use direct uppercase char value
                        Config::KeySpamTrigger = static_cast<int>(toupper(value[0]));
                        std::cerr << "Warning: Could not map config key '" << value[0] << "' via VkKeyScanA. Using direct char value." << std::endl;
                    }
                }
            }
        }
        catch (const std::exception& e) { // Catch std::invalid_argument, std::out_of_range
            std::cerr << "Warning: Invalid config value on line " << lineNumber << " for key '" << key << "': " << value << " (" << e.what() << ")" << std::endl;
        }
    }
    configFile.close();

    // Output loaded configuration values using std::cout
    std::cout << "Configuration loaded:" << std::endl;
    std::cout << "  SPAM_DELAY_MS: " << Config::SpamDelayMs.load() << std::endl;
    std::cout << "  SPAM_KEY_DOWN_DURATION: " << Config::SpamKeyDownDurationMs.load() << std::endl;
    std::cout << "  isLocked: " << (Config::IsLocked.load() ? "true" : "false") << std::endl;
    std::cout << "  isWASDStrafingEnabled: " << (Config::IsWASDStrafingEnabled.load() ? "true" : "false") << std::endl;
    std::cout << "  KEY_SPAM_TRIGGER: " << static_cast<char>(Config::KeySpamTrigger.load()) << " (VK: " << Config::KeySpamTrigger.load() << ")" << std::endl;
}

// --- Window Procedure and Tray Icon ---

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        // Window created, could do post-creation init if needed
        break;

    case Globals::WM_TRAYICON: // Message received from tray icon
        switch (LOWORD(lParam)) { // Check the mouse event
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            ShowContextMenu(hwnd);
            break;

            // case NIN_BALLOONUSERCLICK: // Example: Handle click on balloon tip
            //    break;

            // case WM_LBUTTONUP: // Handle left click if desired
            //    break;
        }
        break;

        // WM_COMMAND is not used if handling menu clicks directly after TrackPopupMenu

    case WM_CLOSE:
        // User somehow tried to close the hidden window (e.g., task manager)
        std::cout << "WM_CLOSE received, initiating shutdown." << std::endl;
        DestroyWindow(hwnd); // Trigger standard cleanup via WM_DESTROY
        break;

    case WM_DESTROY:
        // Window is being destroyed, perform final cleanup
        std::cout << "WM_DESTROY received. Cleaning up application..." << std::endl;
        CleanupApplication(); // Perform all necessary cleanup
        PostQuitMessage(0);   // Terminate the message loop
        break;

    default:
        // Let the default window procedure handle other messages
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0; // Message was processed
}

void InitNotifyIconData(HWND hwnd) {
    ZeroMemory(&Globals::g_nid, sizeof(Globals::g_nid));
    Globals::g_nid.cbSize = sizeof(NOTIFYICONDATA);
    Globals::g_nid.hWnd = hwnd;
    Globals::g_nid.uID = Globals::ID_TRAY_APP_ICON;
    Globals::g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; // Base flags
    Globals::g_nid.uCallbackMessage = Globals::WM_TRAYICON; // Our custom message ID

    // Load icon (using standard application icon)
    Globals::g_nid.hIcon = (HICON)LoadImage(Globals::g_hInstance, IDI_APPLICATION, IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    if (!Globals::g_nid.hIcon) {
        Globals::g_nid.hIcon = LoadIcon(NULL, IDI_WARNING); // Fallback
    }

    // Set tooltip text using safe string function
    // Use %hs format specifier to print const char* into TCHAR buffer
    StringCchPrintf(Globals::g_nid.szTip, ARRAYSIZE(Globals::g_nid.szTip), TEXT("%hs v%hs"),
        Config::APP_NAME, Config::VERSION);

    // Add the icon to the tray
    if (!Shell_NotifyIcon(NIM_ADD, &Globals::g_nid)) {
        LogError("Shell_NotifyIcon(NIM_ADD) failed", GetLastError());
    }

    // Set version for modern features (like correct message reporting)
    Globals::g_nid.uVersion = NOTIFYICON_VERSION_4;
    if (!Shell_NotifyIcon(NIM_SETVERSION, &Globals::g_nid)) {
        // This might fail on older systems, not critical
        // LogError("Shell_NotifyIcon(NIM_SETVERSION) failed", GetLastError());
        std::cerr << "Warning: Failed to set tray icon version 4." << std::endl;
    }
}

void ShowContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd); // Necessary for menu interaction

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    // Build Menu
    // Use TEXT macro for menu item strings passed to AppendMenu
    UINT checkFlag = Config::IsWASDStrafingEnabled.load(std::memory_order_relaxed) ? MF_CHECKED : MF_UNCHECKED;
    AppendMenu(hMenu, MF_STRING | checkFlag, Globals::ID_TRAY_TOGGLE_STRAFING_ITEM, TEXT("Enable WASD Strafing"));
    AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenu(hMenu, MF_STRING, Globals::ID_TRAY_EXIT_MENU_ITEM, TEXT("Exit"));

    // Display and track selection
    // TPM_RETURNCMD blocks until selection and returns the command ID
    int commandId = TrackPopupMenu(hMenu,
        TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
        pt.x, pt.y, 0, hwnd, NULL);

    DestroyMenu(hMenu); // Clean up menu resource

    // Process selection immediately
    switch (commandId) {
    case Globals::ID_TRAY_TOGGLE_STRAFING_ITEM:
    {
        bool newState = !Config::IsWASDStrafingEnabled.load(std::memory_order_relaxed);
        Config::IsWASDStrafingEnabled.store(newState, std::memory_order_relaxed);
        std::cout << "WASD Strafing " << (newState ? "Enabled" : "Disabled") << std::endl;
        if (!newState) {
            // If disabling, ensure spam stops cleanly BUT DON'T restore keys
            // as user explicitly disabled the feature.
            CleanupSpamState(false); // false = don't restore held keys state
        }
    }
    break;
    case Globals::ID_TRAY_EXIT_MENU_ITEM:
        std::cout << "Exit command selected from tray menu." << std::endl;
        DestroyWindow(hwnd); // Initiate shutdown process -> WM_DESTROY
        break;
    }

    // Post dummy message to release foreground focus properly
    PostMessage(hwnd, WM_NULL, 0, 0);
}

// --- Utility Functions ---

void LogError(const std::string& message, DWORD errorCode) {
    std::cerr << "ERROR: " << message;
    if (errorCode == 0) {
        errorCode = GetLastError(); // Get error code if not provided and not 0 already
    }
    if (errorCode != 0) {
        LPSTR messageBuffer = nullptr;
        // Use FORMAT_MESSAGE_IGNORE_INSERTS for safety
        size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

        if (size > 0 && messageBuffer != nullptr) {
            std::string systemMessage(messageBuffer, size);
            // Trim trailing newline characters which FormatMessage often adds
            while (!systemMessage.empty() && (systemMessage.back() == '\n' || systemMessage.back() == '\r')) {
                systemMessage.pop_back();
            }
            std::cerr << " (Code: " << errorCode << " - " << systemMessage << ")";
        }
        else {
            std::cerr << " (Code: " << errorCode << " - FormatMessage failed)";
        }
        LocalFree(messageBuffer); // Free the buffer allocated by FormatMessage
    }
    std::cerr << std::endl;

    // Optionally show a message box in release builds?
#ifndef _DEBUG
// std::string fullMsg = message + " (Error Code: " + std::to_string(errorCode) + ")";
// MessageBoxA(NULL, fullMsg.c_str(), "StrafeHelper Error", MB_OK | MB_ICONERROR);
#endif
}