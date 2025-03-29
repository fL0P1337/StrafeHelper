// KeyboardHook.cpp
#include "KeyboardHook.h"
#include "Globals.h"      // <-- Add
#include "Config.h"
#include "SpamLogic.h"
#include "Utils.h"
#include <vector>
#include <algorithm>      // <-- Add
#include <iostream>
#include <windows.h>      // <-- Add (needed for KBDLLHOOKSTRUCT etc)

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode != HC_ACTION) {
        return CallNextHookEx(Globals::g_hHook, nCode, wParam, lParam); // Globals::
    }

    KBDLLHOOKSTRUCT* pKeybd = (KBDLLHOOKSTRUCT*)lParam;
    int vkCode = pKeybd->vkCode;
    bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    bool isKeyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

    if (pKeybd->flags & LLKHF_INJECTED) {
        return CallNextHookEx(Globals::g_hHook, nCode, wParam, lParam); // Globals::
    }

    // --- Declare itKeyInfo *before* first use ---
    auto itKeyInfo = Globals::g_KeyInfo.find(vkCode); // Globals::
    bool isManagedKey = (itKeyInfo != Globals::g_KeyInfo.end()); // Globals::
    // ---

    bool isWASD = (vkCode == 'W' || vkCode == 'A' || vkCode == 'S' || vkCode == 'D');
    int currentTriggerKey = Config::KeySpamTrigger.load(std::memory_order_relaxed);
    bool isTrigger = (vkCode == currentTriggerKey);

    if (isManagedKey) {
        // itKeyInfo is now initialized before this block
        itKeyInfo->second.physicalKeyDown.store(isKeyDown, std::memory_order_relaxed);
    }

    if (isTrigger && Config::IsWASDStrafingEnabled.load(std::memory_order_relaxed)) {
        if (isKeyDown) {
            if (!Globals::g_isCSpamActive.load(std::memory_order_relaxed)) { // Globals::
                Globals::g_isCSpamActive.store(true, std::memory_order_relaxed); // Globals::
                std::cout << "Spam Activated" << std::endl;

                std::vector<int> keysToStartSpamming;
                std::vector<INPUT> keyUpInputs;

                for (int key : {'W', 'A', 'S', 'D'}) {
                    // --- Re-add KeyState& declaration ---
                    Globals::KeyState& keyStateRef = Globals::g_KeyInfo[key]; // Globals::
                    // ---
                    if (keyStateRef.physicalKeyDown.load(std::memory_order_relaxed)) { // Use variable
                        keyStateRef.spamming.store(true, std::memory_order_relaxed); // Use variable
                        keysToStartSpamming.push_back(key);

                        INPUT input = { INPUT_KEYBOARD };
                        input.ki.wVk = key;
                        input.ki.wScan = MapVirtualKey(key, MAPVK_VK_TO_VSC);
                        input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
                        input.ki.dwExtraInfo = GetMessageExtraInfo();
                        keyUpInputs.push_back(input);
                    }
                    else {
                        keyStateRef.spamming.store(false, std::memory_order_relaxed); // Use variable
                    }
                }

                if (!keyUpInputs.empty()) {
                    SendInput(static_cast<UINT>(keyUpInputs.size()), keyUpInputs.data(), sizeof(INPUT));
                }

                EnterCriticalSection(&Globals::g_csActiveKeys); // Globals::
                Globals::g_activeSpamKeys = keysToStartSpamming; // Globals::
                LeaveCriticalSection(&Globals::g_csActiveKeys); // Globals::

                SetEvent(Globals::g_hSpamEvent); // Globals::
            }
            // return 1;
        }
        else if (isKeyUp) {
            if (Globals::g_isCSpamActive.load(std::memory_order_relaxed)) { // Globals::
                std::cout << "Spam Deactivated" << std::endl;
                CleanupSpamState(true);
            }
        }
        return CallNextHookEx(Globals::g_hHook, nCode, wParam, lParam); // Globals::
    }

    if (isWASD && Config::IsWASDStrafingEnabled.load(std::memory_order_relaxed) && Globals::g_isCSpamActive.load(std::memory_order_relaxed)) { // Globals::
        // --- Re-add KeyState& declaration ---
        // itKeyInfo is already declared and found/not found checked above
        if (!isManagedKey) return CallNextHookEx(Globals::g_hHook, nCode, wParam, lParam); // Should not happen if WASD check passes, but safer
        Globals::KeyState& keyState = itKeyInfo->second; // Globals::
        // ---

        if (isKeyDown) {
            if (!keyState.spamming.load(std::memory_order_relaxed)) { // Use variable
                keyState.spamming.store(true, std::memory_order_relaxed); // Use variable

                EnterCriticalSection(&Globals::g_csActiveKeys); // Globals::
                // Use std::find correctly
                if (std::find(Globals::g_activeSpamKeys.begin(), Globals::g_activeSpamKeys.end(), vkCode) == Globals::g_activeSpamKeys.end()) { // Globals::
                    Globals::g_activeSpamKeys.push_back(vkCode); // Globals::
                }
                LeaveCriticalSection(&Globals::g_csActiveKeys); // Globals::

                INPUT input = { INPUT_KEYBOARD };
                input.ki.wVk = vkCode;
                input.ki.wScan = MapVirtualKey(vkCode, MAPVK_VK_TO_VSC);
                input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
                input.ki.dwExtraInfo = GetMessageExtraInfo();
                SendInput(1, &input, sizeof(INPUT));

                SetEvent(Globals::g_hSpamEvent); // Globals::
            }
            return 1;
        }
        else if (isKeyUp) {
            if (keyState.spamming.load(std::memory_order_relaxed)) { // Use variable
                keyState.spamming.store(false, std::memory_order_relaxed); // Use variable

                EnterCriticalSection(&Globals::g_csActiveKeys); // Globals::
                // Fix std::remove usage within erase-remove idiom
                Globals::g_activeSpamKeys.erase( // Globals::
                    std::remove(Globals::g_activeSpamKeys.begin(), Globals::g_activeSpamKeys.end(), vkCode), // <-- Fix arguments
                    Globals::g_activeSpamKeys.end() // Globals::
                );
                LeaveCriticalSection(&Globals::g_csActiveKeys); // Globals::

                INPUT input = { INPUT_KEYBOARD };
                input.ki.wVk = vkCode;
                input.ki.wScan = MapVirtualKey(vkCode, MAPVK_VK_TO_VSC);
                input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
                input.ki.dwExtraInfo = GetMessageExtraInfo();
                SendInput(1, &input, sizeof(INPUT));

                SetEvent(Globals::g_hSpamEvent); // Globals::
            }
            return 1;
        }
    }

    return CallNextHookEx(Globals::g_hHook, nCode, wParam, lParam); // Globals::
}

bool SetupKeyboardHook(HINSTANCE hInstance) {
    // Use Globals::g_hInstance if Application::InitializeApplication sets it first
    Globals::g_hHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, Globals::g_hInstance, 0); // Globals:: (both)
    if (!Globals::g_hHook) { // Globals::
        LogError("SetWindowsHookEx failed");
        return false;
    }
    std::cout << "Keyboard hook installed." << std::endl;
    return true;
}

void TeardownKeyboardHook() {
    if (Globals::g_hHook) { // Globals::
        if (UnhookWindowsHookEx(Globals::g_hHook)) { // Globals::
            std::cout << "Keyboard hook uninstalled." << std::endl;
        }
        else {
            LogError("UnhookWindowsHookEx failed");
        }
        Globals::g_hHook = NULL; // Globals::
    }
}