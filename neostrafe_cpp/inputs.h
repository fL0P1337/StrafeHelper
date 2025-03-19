#pragma once
#include <windows.h>
#include <atomic>
#include <vector>
#include <mutex>
#include "Config.h"
#include <chrono>
#include <unordered_map>
#include <algorithm>

namespace inputs {

    // Forward declarations for thread and keyboard hook callback functions.
    static LRESULT CALLBACK KeyboardProcImpl(int nCode, WPARAM wParam, LPARAM lParam);
    static DWORD WINAPI SpamThreadProc(LPVOID lpParam);

    // Key state structure
    struct KeyState {
        std::atomic<bool> physicalKeyDown;
        std::atomic<bool> spamActive;
        std::atomic<bool> wasPhysicallyDown;

        KeyState() : physicalKeyDown(false), spamActive(false), wasPhysicallyDown(false) {}
    };

    class InputManager {
    private:
        static InputManager* instance;
        HHOOK hHook;
        std::unordered_map<int, KeyState*> keyInfo;
        std::vector<int> activeSpamKeys;
        std::mutex spamKeysMutex;
        HANDLE hSpamEvent;
        HANDLE hTerminateEvent;
        HANDLE hSpamThreadHandle;
        std::atomic<bool> isCSpamActive;

        InputManager() : hHook(NULL),
            hSpamEvent(NULL), hTerminateEvent(NULL), hSpamThreadHandle(NULL),
            isCSpamActive(false)
        {
            initializeKeyStates();
        }

        DWORD spamThread();
        friend DWORD WINAPI SpamThreadProc(LPVOID lpParam);
        friend LRESULT CALLBACK KeyboardProcImpl(int nCode, WPARAM wParam, LPARAM lParam);

    public:
        static InputManager* getInstance();
        void initializeKeyStates();
        bool initRawInput(HWND hwnd);
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

    bool InputManager::initRawInput(HWND hwnd) {
        RAWINPUTDEVICE rid;
        rid.usUsagePage = 0x01;  // Generic Desktop Controls
        rid.usUsage = 0x06;      // Keyboard
        rid.dwFlags = RIDEV_INPUTSINK;  // Receive input even when not in foreground
        rid.hwndTarget = hwnd;
        return RegisterRawInputDevices(&rid, 1, sizeof(rid));
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

        Sleep(1);  // Small delay to ensure key up is registered

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

        if (keyCode == Config::getInstance()->spamTriggerKey.load() && !isInjected) {
            if (isKeyDown && !isCSpamActive) {
                isCSpamActive = true;
                {
                    std::lock_guard<std::mutex> lock(spamKeysMutex);
                    activeSpamKeys.clear();
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
                restoreKeyStates();
                SetEvent(hSpamEvent);
            }
            return 1;
        }

        if ((keyCode == 'W' || keyCode == 'A' || keyCode == 'S' || keyCode == 'D') && !isInjected) {
            auto it = keyInfo.find(keyCode);
            if (it != keyInfo.end()) {
                it->second->physicalKeyDown = isKeyDown;

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
                            sendGameInput(keyCode, false);
                            Sleep(1);
                        }
                    }
                    SetEvent(hSpamEvent);
                    return 1;
                }
            }
        }

        return CallNextHookEx(hHook, nCode, wParam, lParam);
    }

    DWORD InputManager::spamThread() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        SetThreadAffinityMask(GetCurrentThread(), 1);

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
                            isCSpamActive) {
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
