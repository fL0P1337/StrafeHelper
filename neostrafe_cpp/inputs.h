// inputs.h
#pragma once
#include <windows.h>
#include <atomic>
#include <vector>
#include <mutex>
#include "Config.h"
#include <unordered_map>
#include <algorithm>

namespace inputs {

    // Forward declarations for thread and keyboard hook callback functions.
    static LRESULT CALLBACK KeyboardProcImpl(int nCode, WPARAM wParam, LPARAM lParam);
    static DWORD WINAPI SpamThreadProc(LPVOID lpParam);

    // Key state structure
    struct KeyState {
        std::atomic<bool> physicalKeyDown{ false };
        std::atomic<bool> spamActive{ false };
        std::atomic<bool> wasPhysicallyDown{ false };
    };

    class InputManager {
    private:
        HHOOK hHook{};
        std::unordered_map<int, KeyState*> keyInfo;
        std::vector<int> activeSpamKeys;
        std::mutex spamKeysMutex;
        HANDLE hSpamEvent{};
        HANDLE hTerminateEvent{};
        HANDLE hSpamThreadHandle{};
        std::atomic<bool> isCSpamActive{ false };

        InputManager() {
            initializeKeyStates();
        }
        ~InputManager() = default; // Cleanup is still performed via cleanup()

        DWORD spamThread();
        friend DWORD WINAPI SpamThreadProc(LPVOID lpParam);
        friend LRESULT CALLBACK KeyboardProcImpl(int nCode, WPARAM wParam, LPARAM lParam);

    public:
        // Thread-safe modern singleton using function local static
        static InputManager* getInstance() {
            static InputManager instance;
            return &instance;
        }

        void initializeKeyStates() {
            for (int key : { 'W', 'A', 'S', 'D' })
                keyInfo[key] = new KeyState();
        }

        bool initRawInput(HWND hwnd) {
            RAWINPUTDEVICE rid = { 0 };
            rid.usUsagePage = 0x01;  // Generic Desktop Controls
            rid.usUsage = 0x06;      // Keyboard
            rid.dwFlags = RIDEV_INPUTSINK;  // Receive input even when not in foreground
            rid.hwndTarget = hwnd;
            return RegisterRawInputDevices(&rid, 1, sizeof(rid));
        }

        bool initializeEvents() {
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

        bool setupKeyboardHook() {
            hHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProcImpl, NULL, 0);
            return (hHook != NULL);
        }

        void cleanup() {
            if (hTerminateEvent)
                SetEvent(hTerminateEvent);

            if (hSpamThreadHandle) {
                WaitForSingleObject(hSpamThreadHandle, 1000);
                CloseHandle(hSpamThreadHandle);
                hSpamThreadHandle = NULL;
            }

            if (hHook) {
                UnhookWindowsHookEx(hHook);
                hHook = NULL;
            }

            if (hSpamEvent) { CloseHandle(hSpamEvent); hSpamEvent = NULL; }
            if (hTerminateEvent) { CloseHandle(hTerminateEvent); hTerminateEvent = NULL; }

            for (auto& pair : keyInfo) {
                delete pair.second;
            }
            keyInfo.clear();
        }

        static void sendGameInput(int targetKey, bool keyDown) {
            INPUT input = { 0 };
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = targetKey;
            input.ki.wScan = MapVirtualKey(targetKey, 0);
            input.ki.dwFlags = keyDown ? 0 : KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
        }

        void restoreKeyStates() {
            std::lock_guard<std::mutex> lock(spamKeysMutex);
            for (int key : activeSpamKeys) {
                sendGameInput(key, false);
                if (auto it = keyInfo.find(key); it != keyInfo.end())
                    it->second->spamActive = false;
            }
            Sleep(1);  // ensure key up is registered
            for (int key : { 'W', 'A', 'S', 'D' }) {
                if (auto it = keyInfo.find(key); it != keyInfo.end() && it->second->physicalKeyDown)
                    sendGameInput(key, true);
            }
            activeSpamKeys.clear();
        }

        LRESULT processKeyboardInput(int nCode, WPARAM wParam, LPARAM lParam) {
            if (nCode < 0)
                return CallNextHookEx(hHook, nCode, wParam, lParam);

            KBDLLHOOKSTRUCT* pKeybd = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
            int keyCode = pKeybd->vkCode;
            bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            bool isKeyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
            bool isInjected = (pKeybd->flags & LLKHF_INJECTED) != 0;

            // Handle spam trigger key event
            if (keyCode == Config::getInstance()->spamTriggerKey.load() && !isInjected) {
                if (isKeyDown && !isCSpamActive.load()) {
                    isCSpamActive.store(true);
                    {
                        std::lock_guard<std::mutex> lock(spamKeysMutex);
                        activeSpamKeys.clear();
                        for (int key : { 'W', 'A', 'S', 'D' }) {
                            if (auto it = keyInfo.find(key); it != keyInfo.end() && it->second->physicalKeyDown) {
                                activeSpamKeys.push_back(key);
                                it->second->spamActive = true;
                            }
                        }
                    }
                    SetEvent(hSpamEvent);
                }
                else if (isKeyUp && isCSpamActive.load()) {
                    isCSpamActive.store(false);
                    restoreKeyStates();
                    SetEvent(hSpamEvent);
                }
                return 1;
            }

            // Process physical WASD keys if not injected
            if ((keyCode == 'W' || keyCode == 'A' || keyCode == 'S' || keyCode == 'D') && !isInjected) {
                if (auto it = keyInfo.find(keyCode); it != keyInfo.end()) {
                    it->second->physicalKeyDown = isKeyDown;
                    if (isCSpamActive.load()) {
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

        bool isSpamActive() const { return isCSpamActive.load(); }
        void setSpamActive(bool active) { isCSpamActive.store(active); }
        std::unordered_map<int, KeyState*>& getKeyInfo() { return keyInfo; }
        std::vector<int>& getActiveSpamKeys() { return activeSpamKeys; }
        std::mutex& getSpamKeysMutex() { return spamKeysMutex; }
        HANDLE getSpamEvent() { return hSpamEvent; }
        HANDLE getTerminateEvent() { return hTerminateEvent; }
    };

    DWORD InputManager::spamThread() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        SetThreadAffinityMask(GetCurrentThread(), 1);

        HANDLE events[2] = { hSpamEvent, hTerminateEvent };
        while (true) {
            DWORD waitResult = WaitForMultipleObjects(2, events, FALSE,
                Config::getInstance()->spamDelayMs.load());
            if (waitResult == WAIT_OBJECT_0 + 1)
                break;

            if (Config::getInstance()->isWASDStrafingEnabled.load() && isCSpamActive.load()) {
                std::vector<int> keysToSpam;
                {
                    std::lock_guard<std::mutex> lock(spamKeysMutex);
                    keysToSpam = activeSpamKeys;
                }
                if (!keysToSpam.empty()) {
                    // Release phase
                    for (int key : keysToSpam) {
                        if (auto it = keyInfo.find(key); it != keyInfo.end() && it->second->spamActive)
                            sendGameInput(key, false);
                    }
                    Sleep(Config::getInstance()->spamKeyDownDuration.load());
                    // Press phase: verify key state remains valid before re-pressing.
                    for (int key : keysToSpam) {
                        if (auto it = keyInfo.find(key); it != keyInfo.end() &&
                            it->second->spamActive &&
                            it->second->physicalKeyDown &&
                            isCSpamActive.load())
                        {
                            sendGameInput(key, true);
                        }
                    }
                }
            }
        }
        return 0;
    }

    DWORD WINAPI SpamThreadProc(LPVOID lpParam) {
        return static_cast<InputManager*>(lpParam)->spamThread();
    }

    LRESULT CALLBACK KeyboardProcImpl(int nCode, WPARAM wParam, LPARAM lParam) {
        return InputManager::getInstance()->processKeyboardInput(nCode, wParam, lParam);
    }

} // namespace inputs
