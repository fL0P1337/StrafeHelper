#pragma once
#include <windows.h>
#include <atomic>
#include <map>
#include <vector>
#include <mutex>
#include <dinput.h>
#include "Config.h"
#include <chrono>
#include <unordered_map>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

namespace inputs {
    // Forward declarations
    class InputManager;
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
            isCSpamActive(false) {
            initializeKeyStates();
        }

    public:
        static InputManager* getInstance() {
            if (instance == nullptr) {
                instance = new InputManager();
            }
            return instance;
        }

        void initializeKeyStates() {
            keyInfo['W'] = new KeyState();
            keyInfo['A'] = new KeyState();
            keyInfo['S'] = new KeyState();
            keyInfo['D'] = new KeyState();
        }

        bool initDirectInput(HWND hwnd) {
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

        static void sendGameInput(int targetKey, bool keyDown) {
            INPUT input = { 0 };
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = targetKey;
            input.ki.wScan = MapVirtualKey(targetKey, 0);
            input.ki.dwFlags = keyDown ? 0 : KEYEVENTF_KEYUP;
            input.ki.time = 0;
            input.ki.dwExtraInfo = 0;
            SendInput(1, &input, sizeof(INPUT));
        }

        void restoreKeyStates() {
            std::lock_guard<std::mutex> lock(spamKeysMutex);

            for (int key : activeSpamKeys) {
                sendGameInput(key, false);
            }

            Sleep(1);

            for (int key : activeSpamKeys) {
                auto keyState = keyInfo.find(key);
                if (keyState != keyInfo.end()) {
                    keyState->second->spamActive = false;
                    if (keyState->second->physicalKeyDown) {
                        sendGameInput(key, true);
                    }
                }
            }

            activeSpamKeys.clear();
        }

        LRESULT processKeyboardInput(int nCode, WPARAM wParam, LPARAM lParam) {
            if (nCode < 0)
                return CallNextHookEx(hHook, nCode, wParam, lParam);

            KBDLLHOOKSTRUCT* pKeybd = (KBDLLHOOKSTRUCT*)lParam;
            int keyCode = pKeybd->vkCode;
            bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            bool isKeyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
            bool isInjected = (pKeybd->flags & LLKHF_INJECTED);

            if (keyCode == 'W' || keyCode == 'A' || keyCode == 'S' || keyCode == 'D') {
                auto keyState = keyInfo.find(keyCode);
                if (keyState != keyInfo.end() && !isInjected) {
                    keyState->second->physicalKeyDown = isKeyDown;

                    if (isCSpamActive) {
                        std::lock_guard<std::mutex> lock(spamKeysMutex);
                        if (isKeyDown) {
                            if (std::find(activeSpamKeys.begin(), activeSpamKeys.end(), keyCode) == activeSpamKeys.end()) {
                                activeSpamKeys.push_back(keyCode);
                                keyState->second->spamActive = true;
                            }
                        }
                        else {
                            auto it = std::find(activeSpamKeys.begin(), activeSpamKeys.end(), keyCode);
                            if (it != activeSpamKeys.end()) {
                                activeSpamKeys.erase(it);
                                keyState->second->spamActive = false;
                                sendGameInput(keyCode, false);
                                Sleep(1);
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
                        for (int key : {'W', 'A', 'S', 'D'}) {
                            auto keyState = keyInfo.find(key);
                            if (keyState != keyInfo.end() && keyState->second->physicalKeyDown) {
                                activeSpamKeys.push_back(key);
                                keyState->second->spamActive = true;
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

    private:
        DWORD spamThread() {
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
                    {
                        std::lock_guard<std::mutex> lock(spamKeysMutex);
                        keysToSpam = activeSpamKeys;
                    }

                    if (!keysToSpam.empty()) {
                        for (int key : keysToSpam) {
                            auto keyState = keyInfo.find(key);
                            if (keyState != keyInfo.end() && keyState->second->spamActive) {
                                sendGameInput(key, false);
                            }
                        }

                        Sleep(Config::getInstance()->spamKeyDownDuration.load());

                        for (int key : keysToSpam) {
                            auto keyState = keyInfo.find(key);
                            if (keyState != keyInfo.end() &&
                                keyState->second->spamActive &&
                                keyState->second->physicalKeyDown) {
                                sendGameInput(key, true);
                            }
                        }
                    }
                }
            }
            return 0;
        }

        friend DWORD WINAPI SpamThreadProc(LPVOID lpParam);
        friend LRESULT CALLBACK KeyboardProcImpl(int nCode, WPARAM wParam, LPARAM lParam);
    };

    InputManager* InputManager::instance = nullptr;

    static DWORD WINAPI SpamThreadProc(LPVOID lpParam) {
        return ((InputManager*)lpParam)->spamThread();
    }

    static LRESULT CALLBACK KeyboardProcImpl(int nCode, WPARAM wParam, LPARAM lParam) {
        return InputManager::getInstance()->processKeyboardInput(nCode, wParam, lParam);
    }
}