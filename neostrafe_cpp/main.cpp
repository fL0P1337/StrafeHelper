#include <windows.h>
#include <shellapi.h>
#include <map>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iostream>
#include <atomic> // For atomic booleans
#include <tchar.h> // For TCHAR and related macros like _T, TEXT
#include <strsafe.h> // For StringCchPrintf (safer alternative to wsprintf)


// Constants for tray icon and menu
#define ID_TRAY_APP_ICON                1001
#define ID_TRAY_EXIT_CONTEXT_MENU_ITEM  3000
#define ID_TRAY_WASD_STRAFING           3002
#define WM_TRAYICON                     (WM_USER + 1)

// Application details
const char* APP_NAME = "StrafeHelper";
const char* VERSION = "1.1_ghostfix_compiled"; // Updated version string

// Configuration defaults (mutable, so they can be overwritten from config file)
int SPAM_DELAY_MS = 10;         // Delay between spamming cycles
int SPAM_KEY_DOWN_DURATION = 5; // Duration (in ms) keys remain down in a cycle

// Global configurable booleans
std::atomic<bool> isLocked = false;            // Global toggle: spam feature enabled/disabled (unused in current logic, but kept from original)
std::atomic<bool> isCSpamActive = false;       // Spam trigger active flag
std::atomic<bool> isWASDStrafingEnabled = true; // Toggle for WASD Strafing

// Keybind customization
int KEY_SPAM_TRIGGER = 'C';      // Default key to trigger WASD spamming (Use uppercase VK code)

// Structure to track key states
struct KeyState {
    std::atomic<bool> physicalKeyDown = false;  // Is the key physically pressed?
    std::atomic<bool> spamming = false;         // Is this key currently being managed by the spam thread?

    // Since std::atomic makes the struct non-copyable/non-assignable,
    // we don't need to explicitly delete constructors/assignment operators,
    // but it's good to be aware. We'll use emplace to add these to the map.
    KeyState() : physicalKeyDown(false), spamming(false) {} // Default constructor needed for map emplace/[]
};

// Global variables
HHOOK hHook = NULL;
NOTIFYICONDATA nid;
std::map<int, KeyState> KeyInfo;   // Track state for keys: WASD (Maps VK code to KeyState)
std::vector<int> activeSpamKeys;   // Currently active keys for strafing (Protected by csActiveKeys)
CRITICAL_SECTION csActiveKeys;     // Critical section to protect access to activeSpamKeys

// Event to signal spam thread for changes
HANDLE hSpamEvent = NULL;

// Forward declarations
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void InitNotifyIconData(HWND hwnd);
void SendKeyInput(int targetKey, bool keyDown);
DWORD WINAPI SpamThread(LPVOID lpParam);
void loadConfig();
void CleanupSpamState();

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
        std::istringstream lineStream(line); // Use istringstream for easier parsing
        std::string key;
        if (line.empty() || line[0] == '#')
            continue;

        size_t pos = line.find('=');
        if (pos == std::string::npos)
            continue; // Malformed line, skip

        key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        // Trim whitespace (safer approach)
        key.erase(0, key.find_first_not_of(" \t\n\r\f\v"));
        key.erase(key.find_last_not_of(" \t\n\r\f\v") + 1);
        value.erase(0, value.find_first_not_of(" \t\n\r\f\v"));
        value.erase(value.find_last_not_of(" \t\n\r\f\v") + 1);


        try {
            if (key == "SPAM_DELAY_MS")
                SPAM_DELAY_MS = std::stoi(value);
            else if (key == "SPAM_KEY_DOWN_DURATION")
                SPAM_KEY_DOWN_DURATION = std::stoi(value);
            else if (key == "isLocked")
                isLocked = (value == "true" || value == "1");
            else if (key == "isWASDStrafingEnabled")
                isWASDStrafingEnabled = (value == "true" || value == "1");
            else if (key == "KEY_SPAM_TRIGGER") {
                // Ensure value is not empty and convert first char to uppercase VK code
                if (!value.empty()) {
                    // Get the virtual key code for the character
                    SHORT vkShort = VkKeyScanA(value[0]);
                    if (vkShort != -1) {
                        KEY_SPAM_TRIGGER = LOBYTE(vkShort); // Extract VK code
                    }
                    else {
                        // Fallback or default if VkKeyScanA fails
                        KEY_SPAM_TRIGGER = static_cast<int>(toupper(value[0]));
                        std::cerr << "Warning: Could not map config key '" << value[0] << "' to VK code via VkKeyScanA. Using direct char value." << std::endl;
                    }
                }
            }
        }
        catch (const std::invalid_argument& e) {
            std::cerr << "Warning: Invalid config value for key '" << key << "': " << value << " (" << e.what() << ")" << std::endl;
        }
        catch (const std::out_of_range& e) {
            std::cerr << "Warning: Config value out of range for key '" << key << "': " << value << " (" << e.what() << ")" << std::endl;
        }
    }
    configFile.close();

    // Output loaded configuration values
    std::cout << "Configuration loaded:" << std::endl;
    std::cout << "  SPAM_DELAY_MS: " << SPAM_DELAY_MS << std::endl;
    std::cout << "  SPAM_KEY_DOWN_DURATION: " << SPAM_KEY_DOWN_DURATION << std::endl;
    std::cout << "  isLocked: " << (isLocked ? "true" : "false") << std::endl;
    std::cout << "  isWASDStrafingEnabled: " << (isWASDStrafingEnabled ? "true" : "false") << std::endl;
    std::cout << "  KEY_SPAM_TRIGGER: " << static_cast<char>(KEY_SPAM_TRIGGER) << " (VK: " << KEY_SPAM_TRIGGER << ")" << std::endl;
}

// Main function
int main() {
#ifdef _DEBUG // Only allocate console in debug builds usually
    if (!GetConsoleWindow()) { // Check if console already exists
        AllocConsole();
        FILE* pCout; // Use FILE* for freopen_s
        freopen_s(&pCout, "CONOUT$", "w", stdout);
        FILE* pCerr; // Redirect stderr as well
        freopen_s(&pCerr, "CONOUT$", "w", stderr);
        SetConsoleTitle(TEXT("StrafeHelper Console")); // Use TEXT macro
    }
#endif

    // Initialize critical section
    InitializeCriticalSection(&csActiveKeys);

    // Load configuration from file
    loadConfig();

    // Create an event for the spam thread. Auto-reset.
    hSpamEvent = CreateEvent(NULL, FALSE, FALSE, NULL); // Auto-reset, initially non-signaled

    // Register window class
    WNDCLASSEX wc = { 0 };
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = TEXT("StrafeHelperClass"); // Use TEXT macro
    // Load icon properly (consider adding an icon resource to your project)
    wc.hIcon = (HICON)LoadImage(wc.hInstance, IDI_APPLICATION, IMAGE_ICON, 0, 0, LR_SHARED);
    wc.hIconSm = (HICON)LoadImage(wc.hInstance, IDI_APPLICATION, IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION); // Fallback
    if (!wc.hIconSm) wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION); // Fallback
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    if (!RegisterClassEx(&wc)) {
        MessageBox(NULL, TEXT("Window Registration Failed!"), TEXT("Error"), MB_ICONEXCLAMATION | MB_OK);
        DeleteCriticalSection(&csActiveKeys);
        if (hSpamEvent) CloseHandle(hSpamEvent);
        return 1;
    }

    // Create a minimal hidden window for message processing
    HWND hwnd = CreateWindowEx(
        WS_EX_TOOLWINDOW, // Makes it hidden from taskbar and Alt+Tab
        wc.lpszClassName,
        TEXT("StrafeHelper Hidden Window"), // Use TEXT macro
        WS_POPUP, // Use WS_POPUP for a window that isn't meant to be visible
        0, 0, 0, 0, // Position and size don't matter much for a hidden message window
        NULL, NULL, wc.hInstance, NULL);

    if (!hwnd) {
        MessageBox(NULL, TEXT("Window Creation Failed!"), TEXT("Error"), MB_ICONEXCLAMATION | MB_OK);
        UnregisterClass(wc.lpszClassName, wc.hInstance); // Clean up class registration
        DeleteCriticalSection(&csActiveKeys);
        if (hSpamEvent) CloseHandle(hSpamEvent);
        return 1;
    }

    // Window is created but not shown (WS_POPUP and WS_EX_TOOLWINDOW help hide it)

    // Initialize tray icon
    InitNotifyIconData(hwnd);

    // Initialize key states for WASD keys using emplace due to std::atomic
    KeyInfo['W']; // Ensures 'W' exists with a default KeyState
    KeyInfo['A']; // Ensures 'A' exists with a default KeyState
    KeyInfo['S']; // Ensures 'S' exists with a default KeyState
    KeyInfo['D']; // Ensures 'D' exists with a default KeyState

    // Ensure trigger key state exists if different from WASD
    if (KEY_SPAM_TRIGGER != 'W' && KEY_SPAM_TRIGGER != 'A' &&
        KEY_SPAM_TRIGGER != 'S' && KEY_SPAM_TRIGGER != 'D')
    {
        KeyInfo[KEY_SPAM_TRIGGER]; // Ensures trigger key exists with a default KeyState
    }


    // Set the global keyboard hook
    hHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);
    if (!hHook) {
        MessageBox(NULL, TEXT("Failed to install hook!"), TEXT("Error"), MB_ICONEXCLAMATION | MB_OK);
        Shell_NotifyIcon(NIM_DELETE, &nid); // Clean up tray icon
        DeleteCriticalSection(&csActiveKeys);
        if (hSpamEvent) CloseHandle(hSpamEvent);
        DestroyWindow(hwnd); // Clean up window
        UnregisterClass(wc.lpszClassName, wc.hInstance); // Clean up class
        return 1;
    }

    // Start thread for spamming
    HANDLE hSpamThread = CreateThread(NULL, 0, SpamThread, NULL, 0, NULL);
    if (!hSpamThread) {
        MessageBox(NULL, TEXT("Failed to create spam thread!"), TEXT("Error"), MB_ICONEXCLAMATION | MB_OK);
        UnhookWindowsHookEx(hHook);
        Shell_NotifyIcon(NIM_DELETE, &nid);
        DeleteCriticalSection(&csActiveKeys);
        if (hSpamEvent) CloseHandle(hSpamEvent);
        DestroyWindow(hwnd);
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    std::cout << "StrafeHelper running. Right-click tray icon to configure or exit." << std::endl;

    // Message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) { // Correct loop condition
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    std::cout << "Exiting StrafeHelper..." << std::endl;

    // Cleanup resources
    UnhookWindowsHookEx(hHook);
    Shell_NotifyIcon(NIM_DELETE, &nid);

    // Signal spam thread to exit might be better with an atomic flag
    // For now, just signal the event and close the handle.
    if (hSpamEvent) SetEvent(hSpamEvent); // Wake up thread one last time if waiting
    if (hSpamThread) {
        // Optionally wait for thread to finish, but might hang if thread is stuck
        // WaitForSingleObject(hSpamThread, 1000); // Wait max 1 second
        CloseHandle(hSpamThread);
    }

    if (hSpamEvent) CloseHandle(hSpamEvent);
    DeleteCriticalSection(&csActiveKeys);
    DestroyWindow(hwnd); // Ensure window is destroyed before unregistering class
    UnregisterClass(wc.lpszClassName, wc.hInstance); // Unregister class on exit

#ifdef _DEBUG
    // Check if pointers are valid before closing (though freopen_s should handle NULL)
    // The FILE* pointers pCout/pCerr only exist in main's scope, so we can't close them here.
    // Closing stdout/stderr isn't usually necessary.
    FreeConsole();
#endif

    return (int)msg.wParam; // Return quit code
}


// Keyboard hook procedure - Revised Logic
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* pKeybd = (KBDLLHOOKSTRUCT*)lParam;
        int vkCode = pKeybd->vkCode;
        bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        bool isInjected = (pKeybd->flags & LLKHF_INJECTED);

        // Always ignore injected events to prevent loops if SendInput is detected as injected
        if (isInjected) {
            return CallNextHookEx(hHook, nCode, wParam, lParam);
        }

        // --- Handle WASD Keys ---
        // Check if vkCode is one of the keys we track (W, A, S, D)
        if (vkCode == 'W' || vkCode == 'A' || vkCode == 'S' || vkCode == 'D') {
            // Update physical state FIRST
            KeyInfo[vkCode].physicalKeyDown = isKeyDown;

            if (isWASDStrafingEnabled && isCSpamActive) {
                if (isKeyDown) {
                    // Key pressed while spam mode is active
                    if (!KeyInfo[vkCode].spamming) {
                        KeyInfo[vkCode].spamming = true;
                        // Add to active list if not already there (under lock)
                        EnterCriticalSection(&csActiveKeys);
                        if (std::find(activeSpamKeys.begin(), activeSpamKeys.end(), vkCode) == activeSpamKeys.end()) {
                            activeSpamKeys.push_back(vkCode);
                        }
                        LeaveCriticalSection(&csActiveKeys);
                        // Send an immediate KEY UP to counteract the physical DOWN that the OS/game just saw
                        SendKeyInput(vkCode, false);
                        SetEvent(hSpamEvent); // Wake spam thread
                    }
                    // Block the original keydown event since we manage it now
                    return 1;
                }
                else {
                    // Key released while spam mode is active
                    if (KeyInfo[vkCode].spamming) {
                        KeyInfo[vkCode].spamming = false;
                        // Remove from active list (under lock)
                        EnterCriticalSection(&csActiveKeys);
                        // Use remove-erase idiom for potentially better performance than find+erase
                        activeSpamKeys.erase(std::remove(activeSpamKeys.begin(), activeSpamKeys.end(), vkCode), activeSpamKeys.end());
                        LeaveCriticalSection(&csActiveKeys);
                        // Ensure the key is seen as UP by sending a simulated UP.
                        SendKeyInput(vkCode, false);
                        SetEvent(hSpamEvent); // Wake spam thread to notice removal
                    }
                    // Block the original keyup because we handled the release
                    return 1;
                }
            }
            // If spam is NOT active for WASD keys, let it pass through normally.
        }

        // --- Handle Spam Trigger Key ---
        if (isWASDStrafingEnabled && vkCode == KEY_SPAM_TRIGGER) {
            // Track physical state of trigger key too, if it's being tracked
            if (KeyInfo.count(KEY_SPAM_TRIGGER)) {
                KeyInfo[KEY_SPAM_TRIGGER].physicalKeyDown = isKeyDown;
            }

            if (isKeyDown) {
                // Check previous key state using GetAsyncKeyState for the trigger key
                // This helps prevent auto-repeat messages from constantly re-triggering
                // Bit 15 (0x8000) is set if the key is down, Bit 1 (0x0001) for toggled state (e.g. caps lock)
                // We only care if it *wasn't* down before this message.
                // Note: GetAsyncKeyState check here might be slightly racy with the hook message,
                // but it's a common technique to filter auto-repeats in hooks.
                // A simpler approach is just checking our atomic flag 'isCSpamActive'.
                if (!isCSpamActive) { // Only activate if not already active
                    isCSpamActive = true;
                    std::cout << "Spam Activated" << std::endl;

                    EnterCriticalSection(&csActiveKeys);
                    activeSpamKeys.clear(); // Start fresh
                    // Check which WASD keys are *currently* physically held down
                    // Use C++11 style iteration
                    for (auto const& pair : KeyInfo) {
                        int key = pair.first;
                        // Check only WASD keys
                        if (key == 'W' || key == 'A' || key == 'S' || key == 'D') {
                            // Access atomic bool directly for reading
                            if (pair.second.physicalKeyDown) {
                                // Cast int key to char for output - safe for ASCII W,A,S,D
                                std::cout << "  Activating spam for held key: " << static_cast<char>(key) << std::endl;
                                // Need to modify the value in the map, cannot modify pair.second directly if const&
                                KeyInfo[key].spamming = true; // Set spamming flag
                                activeSpamKeys.push_back(key);
                                // Send an immediate UP for keys held *before* C was pressed
                                SendKeyInput(key, false);
                            }
                            else {
                                KeyInfo[key].spamming = false; // Ensure state is clean
                            }
                        }
                    }
                    LeaveCriticalSection(&csActiveKeys);
                    SetEvent(hSpamEvent); // Wake spam thread
                }
                // Allow the trigger keydown itself to pass through, e.g., for typing 'C'.
                // Optionally block it if 'C' should ONLY be a modifier: return 1;
            }
            else { // Key Up for Trigger Key
                if (isCSpamActive) {
                    std::cout << "Spam Deactivated" << std::endl;
                    CleanupSpamState(); // Use helper function for cleanup
                    // isCSpamActive is set to false inside CleanupSpamState
                }
                // Allow the trigger keyup itself to pass through.
            }
        }
    }

    // Pass the event to the next hook in the chain
    return CallNextHookEx(hHook, nCode, wParam, lParam);
}

// Helper to clean up spam state (used on trigger release or disabling feature)
void CleanupSpamState() {
    if (!isCSpamActive && activeSpamKeys.empty()) return; // Nothing to do

    isCSpamActive = false; // Set flag immediately

    std::vector<int> keysToRelease;
    std::vector<int> keysToRestoreDown;

    EnterCriticalSection(&csActiveKeys);
    keysToRelease = activeSpamKeys; // Copy keys that were being spammed
    activeSpamKeys.clear();

    // Reset spamming flag and check physical state *after* clearing active list
    // Iterate using C++11 initializer list loop
    for (int key : { 'W', 'A', 'S', 'D' }) { // Use char literals
        if (KeyInfo.count(key)) { // Check if key exists just in case
            // Check if it was marked as spamming (it should be if it was in keysToRelease)
            // and reset the flag.
            if (KeyInfo[key].spamming) {
                KeyInfo[key].spamming = false;
            }
            // Check if physical key is STILL down after spam stopped
            if (KeyInfo[key].physicalKeyDown) {
                keysToRestoreDown.push_back(key);
            }
        }
    }
    LeaveCriticalSection(&csActiveKeys);

    // 1. Send KEY UP for all keys that *were* being spammed
    if (!keysToRelease.empty()) {
        std::vector<INPUT> inputs(keysToRelease.size());
        for (size_t i = 0; i < keysToRelease.size(); ++i) {
            inputs[i].type = INPUT_KEYBOARD;
            inputs[i].ki.wVk = keysToRelease[i];
            inputs[i].ki.wScan = MapVirtualKey(keysToRelease[i], MAPVK_VK_TO_VSC);
            inputs[i].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
            inputs[i].ki.time = 0;
            inputs[i].ki.dwExtraInfo = GetMessageExtraInfo(); // Use standard extra info
        }
        SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
        std::cout << "  Sent final UP for spammed keys." << std::endl;
    }

    // Give OS/App a moment to process the key ups
    Sleep(5); // Increased slightly - adjust if needed

    // 2. Send KEY DOWN for keys that are still physically held
    if (!keysToRestoreDown.empty()) {
        std::vector<INPUT> inputs(keysToRestoreDown.size());
        for (size_t i = 0; i < keysToRestoreDown.size(); ++i) {
            inputs[i].type = INPUT_KEYBOARD;
            inputs[i].ki.wVk = keysToRestoreDown[i];
            inputs[i].ki.wScan = MapVirtualKey(keysToRestoreDown[i], MAPVK_VK_TO_VSC);
            inputs[i].ki.dwFlags = KEYEVENTF_SCANCODE; // Key down
            inputs[i].ki.time = 0;
            inputs[i].ki.dwExtraInfo = GetMessageExtraInfo();
        }
        SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
        std::cout << "  Restored DOWN state for physically held keys: ";
        for (int k : keysToRestoreDown) std::cout << static_cast<char>(k) << " ";
        std::cout << std::endl;
    }

    // No need to SetEvent(hSpamEvent) here, the spam thread will naturally stop looping
    // when it checks isCSpamActive next time.
}


// SpamThread - Revised to use Critical Section and check atomic flags
DWORD WINAPI SpamThread(LPVOID lpParam) {
    // Max 4 keys down + 4 keys up = 8 inputs needed per cycle. Size 16 is safe.
    INPUT inputs[16];
    std::vector<int> currentLocalSpamKeys;

    while (true) {
        // Wait for event signal OR timeout if keys are already active
        // If keys are active, we want to loop based on SPAM_DELAY_MS.
        // If no keys are active, we wait indefinitely until signaled.
        DWORD waitTimeout = INFINITE; // Default wait forever
        EnterCriticalSection(&csActiveKeys);
        bool keysActive = !activeSpamKeys.empty();
        LeaveCriticalSection(&csActiveKeys);

        if (keysActive && isCSpamActive) { // Only use timeout if spam is active and keys are pressed
            waitTimeout = SPAM_DELAY_MS;
        }

        DWORD waitResult = WaitForSingleObject(hSpamEvent, waitTimeout);

        // Check exit conditions or if spam disabled AFTER waking up
        if (!isWASDStrafingEnabled || !isCSpamActive) {
            // If spam got disabled while waiting or running, ensure keys are released cleanly
            // Note: CleanupSpamState should handle this, but an extra check is safe.
            // Maybe add a dedicated exit flag for the thread for robustness.
            continue; // Go back to waiting
        }

        // Get a local copy of keys to spam under lock
        EnterCriticalSection(&csActiveKeys);
        currentLocalSpamKeys = activeSpamKeys;
        LeaveCriticalSection(&csActiveKeys);

        if (currentLocalSpamKeys.empty()) {
            continue; // No keys to spam currently, go back to waiting
        }

        // --- Release Phase ---
        UINT count = 0; // Use UINT for SendInput count
        ZeroMemory(inputs, sizeof(inputs));
        for (int key : currentLocalSpamKeys) {
            // Quick check if key still exists in map (should always unless map modified unexpectedly)
            if (KeyInfo.count(key)) {
                // Check spamming flag (might have been turned off by hook)
                // No need to check physicalKeyDown here, hook manages spamming flag based on it
                if (KeyInfo[key].spamming) { // Check the atomic flag directly
                    if (count < ARRAYSIZE(inputs)) {
                        inputs[count].type = INPUT_KEYBOARD;
                        inputs[count].ki.wVk = key;
                        inputs[count].ki.wScan = MapVirtualKey(key, MAPVK_VK_TO_VSC);
                        inputs[count].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
                        inputs[count].ki.time = 0;
                        inputs[count].ki.dwExtraInfo = GetMessageExtraInfo();
                        count++;
                    }
                    else {
                        std::cerr << "Warning: Input buffer overflow in SpamThread (Release Phase)" << std::endl;
                        break; // Avoid buffer overflow
                    }
                }
            }
        }
        if (count > 0) {
            SendInput(count, inputs, sizeof(INPUT));
        }

        // --- Wait ---
        // Use a precise sleep if available, otherwise standard Sleep
        Sleep(SPAM_KEY_DOWN_DURATION); // Fine for durations > ~15ms typically

        // Re-check state before pressing - C could have been released during sleep
        if (!isWASDStrafingEnabled || !isCSpamActive) {
            continue;
        }
        // Re-acquire the list of keys in case they changed during the sleep
        EnterCriticalSection(&csActiveKeys);
        currentLocalSpamKeys = activeSpamKeys; // Update local copy
        LeaveCriticalSection(&csActiveKeys);

        if (currentLocalSpamKeys.empty()) {
            continue; // Keys might have been released during sleep
        }

        // --- Press Phase ---
        count = 0;
        ZeroMemory(inputs, sizeof(inputs));
        for (int key : currentLocalSpamKeys) {
            if (KeyInfo.count(key)) {
                // Check spamming flag again
                if (KeyInfo[key].spamming) {
                    if (count < ARRAYSIZE(inputs)) {
                        inputs[count].type = INPUT_KEYBOARD;
                        inputs[count].ki.wVk = key;
                        inputs[count].ki.wScan = MapVirtualKey(key, MAPVK_VK_TO_VSC);
                        inputs[count].ki.dwFlags = KEYEVENTF_SCANCODE; // Key down
                        inputs[count].ki.time = 0;
                        inputs[count].ki.dwExtraInfo = GetMessageExtraInfo();
                        count++;
                    }
                    else {
                        std::cerr << "Warning: Input buffer overflow in SpamThread (Press Phase)" << std::endl;
                        break; // Avoid buffer overflow
                    }
                }
            }
        }
        if (count > 0) {
            SendInput(count, inputs, sizeof(INPUT));
        }

        // No explicit Sleep(SPAM_DELAY_MS) here; the wait at the top handles the delay.
    }
    return 0; // Thread function should return a value
}


// Send a simulated key event using SendInput (More reliable)
void SendKeyInput(int targetKey, bool keyDown) {
    INPUT input = { 0 };
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = targetKey;
    input.ki.wScan = MapVirtualKey(targetKey, MAPVK_VK_TO_VSC); // Use scan code
    input.ki.dwFlags = KEYEVENTF_SCANCODE | (keyDown ? 0 : KEYEVENTF_KEYUP);
    input.ki.time = 0;
    // Setting dwExtraInfo might help distinguish injected events, but requires coordination.
    // For now, use the standard info. If filtering injected events becomes complex,
    // consider setting a unique value here and checking it in the hook.
    input.ki.dwExtraInfo = GetMessageExtraInfo();
    SendInput(1, &input, sizeof(INPUT));
}

// Initialize the system tray icon.
void InitNotifyIconData(HWND hwnd) {
    ZeroMemory(&nid, sizeof(nid)); // Ensure it's zeroed out first
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = ID_TRAY_APP_ICON;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_INFO; // Add NIF_INFO for balloon tips if needed
    nid.uCallbackMessage = WM_TRAYICON;

    // Load icon properly (ensure icon is linked or use standard ones)
    HINSTANCE hInstance = GetModuleHandle(NULL);
    nid.hIcon = (HICON)LoadImage(hInstance,
        IDI_APPLICATION, // Standard Application Icon
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_SHARED); // Use LR_SHARED for standard icons
    if (!nid.hIcon) { // Fallback if somehow LoadImage fails
        nid.hIcon = LoadIcon(NULL, IDI_WARNING);
    }

    // Set tooltip text using safer StringCchPrintf and TEXT macro
    TCHAR tipText[128]; // Use TCHAR
    // Use %hs to format const char* into TCHAR buffer
    StringCchPrintf(tipText, ARRAYSIZE(tipText), TEXT("%hs - v%hs"), APP_NAME, VERSION);
    // Copy to nid.szTip (safer than lstrcpy)
    StringCchCopy(nid.szTip, ARRAYSIZE(nid.szTip), tipText);


    // Add the icon to the system tray
    if (!Shell_NotifyIcon(NIM_ADD, &nid)) {
        std::cerr << "Failed to add tray icon!" << std::endl;
    }


    // Set the version for newer notification features (like proper mouse position)
    // Needs to be done *after* NIM_ADD sometimes.
    nid.uVersion = NOTIFYICON_VERSION_4;
    if (!Shell_NotifyIcon(NIM_SETVERSION, &nid)) {
        std::cerr << "Failed to set tray icon version!" << std::endl;
    }

}

// Window procedure for tray menu events.
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        // Optional: Initialization if needed after window creation but before message loop
        break;

    case WM_TRAYICON:
        // Handle tray icon messages (left/right click, etc.)
        // Check the specific mouse message in lParam for version 4+
        switch (LOWORD(lParam)) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU: // Handle right-click
        {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd); // Required for menu dismissal to work correctly

            HMENU hMenu = CreatePopupMenu();
            if (hMenu) {
                // Add menu items - Use MFS_CHECKED/MFS_UNCHECKED with MF_BYCOMMAND
                UINT checkFlag = isWASDStrafingEnabled ? MF_CHECKED : MF_UNCHECKED;
                AppendMenu(hMenu, MF_STRING | checkFlag, ID_TRAY_WASD_STRAFING, TEXT("Enable WASD Strafing"));
                AppendMenu(hMenu, MF_SEPARATOR, 0, NULL);
                AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT_CONTEXT_MENU_ITEM, TEXT("Exit StrafeHelper"));

                // Display the menu and get the selection
                // Using TPM_RETURNCMD means TrackPopupMenu blocks until selection, returning the command ID
                // Or 0 if nothing selected.
                int commandId = TrackPopupMenu(hMenu,
                    TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_RETURNCMD | TPM_NONOTIFY,
                    pt.x, pt.y, 0, hwnd, NULL);

                // Handle the command immediately (no need to post WM_COMMAND)
                if (commandId == ID_TRAY_EXIT_CONTEXT_MENU_ITEM) {
                    std::cout << "Exit command selected." << std::endl;
                    DestroyWindow(hwnd); // This will lead to WM_DESTROY
                }
                else if (commandId == ID_TRAY_WASD_STRAFING) {
                    isWASDStrafingEnabled = !isWASDStrafingEnabled;
                    std::cout << "WASD Strafing " << (isWASDStrafingEnabled ? "Enabled" : "Disabled") << std::endl;
                    if (!isWASDStrafingEnabled) {
                        // If disabling, ensure any active spamming stops cleanly
                        CleanupSpamState();
                    }
                    // No need to update menu checkmark here, it's recreated next time.
                }

                DestroyMenu(hMenu);
                // Post a dummy message to release foreground focus properly after menu closes
                PostMessage(hwnd, WM_NULL, 0, 0);
            }
        }
        break;
        // Handle other tray events like left click if needed
        // case WM_LBUTTONUP:
        //    break;
        }
        break;

        // WM_COMMAND is no longer strictly needed if handled directly after TrackPopupMenu
        // case WM_COMMAND: ...

    case WM_CLOSE:
        // Handle user trying to close the hidden window (e.g., via Task Manager)
        std::cout << "WM_CLOSE received (unexpectedly?). Initiating exit." << std::endl;
        DestroyWindow(hwnd); // Trigger standard exit procedure
        break;

    case WM_DESTROY:
        // Perform cleanup and exit the application
        std::cout << "WM_DESTROY received. Cleaning up." << std::endl;
        // CleanupSpamState(); // Ensure state is clean before exit (might be redundant if Exit command called it)
        PostQuitMessage(0); // Signals the main message loop (GetMessage) to terminate
        break;

    default:
        // Handle any other messages or pass them to the default window procedure
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0; // Message was processed
}