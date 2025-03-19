#pragma once
#include <windows.h>
#include <atomic>
#include <vector>
#include <mutex>
#include <dinput.h>
#include "Config.h"
#include <chrono>
#include <unordered_map>
#include <algorithm>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

namespace inputs {

    // Forward declarations for thread and keyboard hook callback functions.
    static LRESULT CALLBACK KeyboardProcImpl(int nCode, WPARAM wParam, LPARAM lParam);
    static DWORD WINAPI SpamThreadProc(LPVOID lpParam);

    // Key state structure
    struct KeyState {
        std::atomic<bool> physicalKeyDown;
        std::atomic<bool> spamActive;
        std::atomic<bool> wasPhysicallyDown;
        std::chrono::steady_clock::time_point lastStateChange;

        KeyState() : physicalKeyDown(false), spamActive(false), wasPhysicallyDown(false) {
            lastStateChange = std::chrono::steady_clock::now();
        }
    };

    class InputManager {
    private:
        static InputManager* instance;
        LPDIRECTINPUT8 g_pDI;
        LPDIRECTINPUTDEVICE8 g_pKeyboard;
        HHOOK hHook;
        std::unordered_map<int, KeyState*> keyInfo;
        std::vector<int> activeSpamKeys;
        std::mutex spamKeysMutex;
        HANDLE hSpamEvent;
        HANDLE hTerminateEvent;
        HANDLE hSpamThreadHandle;
        std::atomic<bool> isCSpamActive;

        InputManager() : g_pDI(nullptr), g_pKeyboard(nullptr), hHook(NULL),
            hSpamEvent(NULL), hTerminateEvent(NULL), hSpamThreadHandle(NULL),
            isCSpamActive(false)
        {
            initializeKeyStates();
        }

        DWORD spamThread();
        // Friends to allow callbacks access private members
        friend DWORD WINAPI SpamThreadProc(LPVOID lpParam);
        friend LRESULT CALLBACK KeyboardProcImpl(int nCode, WPARAM wParam, LPARAM lParam);
    public:
        static InputManager* getInstance();
        void initializeKeyStates();
        bool initDirectInput(HWND hwnd);
        bool initializeEvents();
        bool setupKeyboardHook();
        void cleanup();
        static void sendGameInput(int targetKey, bool keyDown);
        void restoreKeyStates();

        LRESULT processKeyboardInput(int nCode, WPARAM wParam, LPARAM lParam);
        bool isSpamActive() const { return isCSpamActive.load(); }
        void setSpamActive(bool active) { isCSpamActive.store(active); }
        std::unordered_map<int, KeyState*>& getKeyInfo() { return keyInfo; }
        std::vector<int>& getActiveSpamKeys() { return activeSpamKeys; }
        std::mutex& getSpamKeysMutex() { return spamKeysMutex; }
        HANDLE getSpamEvent() { return hSpamEvent; }
        HANDLE getTerminateEvent() { return hTerminateEvent; }
    };

    // Static member initialization
    InputManager* InputManager::instance = nullptr;

    InputManager* InputManager::getInstance() {
        if (instance == nullptr) {
            instance = new InputManager();
        }
        return instance;
    }

    void InputManager::initializeKeyStates() {
        keyInfo['W'] = new KeyState();
        keyInfo['A'] = new KeyState();
        keyInfo['S'] = new KeyState();
        keyInfo['D'] = new KeyState();
    }

    bool InputManager::initDirectInput(HWND hwnd) {
        if (FAILED(DirectInput8Create(GetModuleHandle(NULL), DIRECTINPUT_VERSION,
            IID_IDirectInput8, (void**)&g_pDI, NULL)))
            return false;

        if (FAILED(g_pDI->CreateDevice(GUID_SysKeyboard, &g_pKeyboard, NULL)))
            return false;

        if (FAILED(g_pKeyboard->SetDataFormat(&c_dfDIKeyboard)))
            return false;

        if (FAILED(g_pKeyboard->SetCooperativeLevel(hwnd,
            DISCL_FOREGROUND | DISCL_NONEXCLUSIVE)))
            return false;

        g_pKeyboard->Acquire();
        return true;
    }

    bool InputManager::initializeEvents() {
        hSpamEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
        hTerminateEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

        if (!hSpamEvent || !hTerminateEvent)
            return false;

        hSpamThreadHandle = CreateThread(NULL, 0, SpamThreadProc, this, 0, NULL);
        if (!hSpamThreadHandle)
            return false;

        SetThreadPriority(hSpamThreadHandle, THREAD_PRIORITY_TIME_CRITICAL);
        return true;
    }

    bool InputManager::setupKeyboardHook() {
        hHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProcImpl, NULL, 0);
        return (hHook != NULL);
    }

    void InputManager::cleanup() {
        if (hTerminateEvent)
            SetEvent(hTerminateEvent);

        if (hSpamThreadHandle) {
            WaitForSingleObject(hSpamThreadHandle, 1000);
            CloseHandle(hSpamThreadHandle);
        }

        if (g_pKeyboard) {
            g_pKeyboard->Unacquire();
            g_pKeyboard->Release();
            g_pKeyboard = nullptr;
        }

        if (g_pDI) {
            g_pDI->Release();
            g_pDI = nullptr;
        }

        if (hHook)
            UnhookWindowsHookEx(hHook);

        if (hSpamEvent) CloseHandle(hSpamEvent);
        if (hTerminateEvent) CloseHandle(hTerminateEvent);

        for (auto& pair : keyInfo) {
            delete pair.second;
        }
        keyInfo.clear();
    }

    void InputManager::sendGameInput(int targetKey, bool keyDown) {
        INPUT input = { 0 };
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = targetKey;
        input.ki.wScan = MapVirtualKey(targetKey, 0);
        input.ki.dwFlags = keyDown ? 0 : KEYEVENTF_KEYUP;
        input.ki.time = 0;
        input.ki.dwExtraInfo = 0;
        SendInput(1, &input, sizeof(INPUT));
    }

    void InputManager::restoreKeyStates() {
        std::lock_guard<std::mutex> lock(spamKeysMutex);

        // First release all active spam keys
        for (int key : activeSpamKeys) {
            sendGameInput(key, false);
            auto it = keyInfo.find(key);
            if (it != keyInfo.end()) {
                it->second->spamActive = false;
            }
        }

        // Small delay to ensure key up is registered
        Sleep(1);

        // Then restore physically held keys
        for (int key : {'W', 'A', 'S', 'D'}) {
            auto it = keyInfo.find(key);
            if (it != keyInfo.end() && it->second->physicalKeyDown) {
                sendGameInput(key, true);
            }
        }

        activeSpamKeys.clear();
    }

    LRESULT InputManager::processKeyboardInput(int nCode, WPARAM wParam, LPARAM lParam) {
        if (nCode < 0)
            return CallNextHookEx(hHook, nCode, wParam, lParam);

        KBDLLHOOKSTRUCT* pKeybd = (KBDLLHOOKSTRUCT*)lParam;
        int keyCode = pKeybd->vkCode;
        bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
        bool isKeyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
        bool isInjected = (pKeybd->flags & LLKHF_INJECTED);

        if (keyCode == 'W' || keyCode == 'A' || keyCode == 'S' || keyCode == 'D') {
            auto it = keyInfo.find(keyCode);
            if (it != keyInfo.end() && !isInjected) {
                it->second->physicalKeyDown = isKeyDown;
                it->second->lastStateChange = std::chrono::steady_clock::now();

                if (isCSpamActive) {
                    std::lock_guard<std::mutex> lock(spamKeysMutex);
                    if (isKeyDown) {
                        if (std::find(activeSpamKeys.begin(), activeSpamKeys.end(), keyCode) == activeSpamKeys.end()) {
                            activeSpamKeys.push_back(keyCode);
                            it->second->spamActive = true;
                        }
                    }
                    else {
                        auto iter = std::find(activeSpamKeys.begin(), activeSpamKeys.end(), keyCode);
                        if (iter != activeSpamKeys.end()) {
                            activeSpamKeys.erase(iter);
                            it->second->spamActive = false;
                            // Ensure key is released
                            sendGameInput(keyCode, false);
                            Sleep(1); // Small delay for stability
                        }
                    }
                    SetEvent(hSpamEvent);
                    return 1;
                }
            }
        }

        if (keyCode == Config::getInstance()->spamTriggerKey.load() && !isInjected) {
            if (isKeyDown && !isCSpamActive) {
                isCSpamActive = true;
                {
                    std::lock_guard<std::mutex> lock(spamKeysMutex);
                    activeSpamKeys.clear();
                    // Only add currently held keys
                    for (int key : {'W', 'A', 'S', 'D'}) {
                        auto it = keyInfo.find(key);
                        if (it != keyInfo.end() && it->second->physicalKeyDown) {
                            activeSpamKeys.push_back(key);
                            it->second->spamActive = true;
                        }
                    }
                }
                SetEvent(hSpamEvent);
            }
            else if (isKeyUp && isCSpamActive) {
                isCSpamActive = false;
                // Force state restoration
                restoreKeyStates();
                SetEvent(hSpamEvent);
            }
        }

        return CallNextHookEx(hHook, nCode, wParam, lParam);
    }

    DWORD InputManager::spamThread() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        SetThreadAffinityMask(GetCurrentThread(), 1);

        std::vector<int> keysToSpam;
        HANDLE events[2] = { hSpamEvent, hTerminateEvent };

        while (true) {
            DWORD waitResult = WaitForMultipleObjects(2, events, FALSE,
                Config::getInstance()->spamDelayMs.load());

            if (waitResult == WAIT_OBJECT_0 + 1)
                break;

            if (Config::getInstance()->isWASDStrafingEnabled.load() && isCSpamActive) {
                std::vector<int> keysToSpam;
                {
                    std::lock_guard<std::mutex> lock(spamKeysMutex);
                    keysToSpam = activeSpamKeys;
                }

                if (!keysToSpam.empty()) {
                    // Release phase
                    for (int key : keysToSpam) {
                        auto it = keyInfo.find(key);
                        if (it != keyInfo.end() && it->second->spamActive) {
                            sendGameInput(key, false);
                        }
                    }

                    Sleep(Config::getInstance()->spamKeyDownDuration.load());

                    // Press phase - verify keys are still valid
                    for (int key : keysToSpam) {
                        auto it = keyInfo.find(key);
                        if (it != keyInfo.end() &&
                            it->second->spamActive &&
                            it->second->physicalKeyDown &&
                            isCSpamActive) { // Additional check
                            sendGameInput(key, true);
                        }
                    }
                }
            }
        }
        return 0;
    }

    DWORD WINAPI SpamThreadProc(LPVOID lpParam) {
        return ((InputManager*)lpParam)->spamThread();
    }

    LRESULT CALLBACK KeyboardProcImpl(int nCode, WPARAM wParam, LPARAM lParam) {
        return InputManager::getInstance()->processKeyboardInput(nCode, wParam, lParam);
    }

} // end namespace inputs
