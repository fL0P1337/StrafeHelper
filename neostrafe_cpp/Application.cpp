// Application.cpp
#include "Application.h"
#include "Globals.h"
#include "Config.h"
#include "Utils.h"
#include "AppWindow.h"
#include "KeyboardHook.h"
#include "SpamLogic.h"
#include "TrayIcon.h"
#include <iostream>
#include <windows.h>
#include <vector> // <-- Add
#include <map>    // <-- Add

// --- Define Global Variables (declared extern in Globals.h) ---
namespace Globals {
    HHOOK g_hHook = NULL;
    HWND g_hWindow = NULL;
    HANDLE g_hSpamThread = NULL;
    HANDLE g_hSpamEvent = NULL;
    HINSTANCE g_hInstance = NULL;

    // Need full types here since it's the definition
    std::map<int, KeyState> g_KeyInfo;
    std::vector<int> g_activeSpamKeys;
    CRITICAL_SECTION g_csActiveKeys; // Initialized in InitializeApplication
    std::atomic<bool> g_isCSpamActive = false;

    NOTIFYICONDATA g_nid = { 0 };
}
// --- End Global Variable Definitions ---


bool InitializeApplication(HINSTANCE hInstance) {
    Globals::g_hInstance = hInstance;

    InitializeCriticalSection(&Globals::g_csActiveKeys); // Use Globals::

    Config::LoadConfig();

    Globals::g_hSpamEvent = CreateEvent(NULL, FALSE, FALSE, NULL); // Use Globals::
    if (!Globals::g_hSpamEvent) {
        LogError("CreateEvent failed");
        DeleteCriticalSection(&Globals::g_csActiveKeys);
        return false;
    }

    if (!CreateAppWindow(hInstance)) {
        CloseHandle(Globals::g_hSpamEvent);
        DeleteCriticalSection(&Globals::g_csActiveKeys);
        return false;
    }

    // Use Globals:: prefix for g_KeyInfo
    Globals::g_KeyInfo['W']; Globals::g_KeyInfo['A'];
    Globals::g_KeyInfo['S']; Globals::g_KeyInfo['D'];
    int triggerKey = Config::KeySpamTrigger.load();
    if (Globals::g_KeyInfo.find(triggerKey) == Globals::g_KeyInfo.end()) {
        Globals::g_KeyInfo[triggerKey];
    }
    std::cout << "Key states initialized." << std::endl;

    InitNotifyIconData(Globals::g_hWindow); // Use Globals::

    if (!SetupKeyboardHook(hInstance)) { // hInstance is okay here
        RemoveTrayIcon();
        // Explicit cleanup needed if Initialize fails mid-way
        if (Globals::g_hWindow) DestroyWindow(Globals::g_hWindow);
        // Ensure class is unregistered if window was created but hook/thread failed
        UnregisterClass(Config::WINDOW_CLASS_NAME, Globals::g_hInstance);
        CloseHandle(Globals::g_hSpamEvent);
        DeleteCriticalSection(&Globals::g_csActiveKeys);
        // Globals::g_hWindow = NULL; // Window handle invalid after DestroyWindow
        Globals::g_hSpamEvent = NULL;
        return false;
    }

    if (!StartSpamThread()) {
        TeardownKeyboardHook();
        RemoveTrayIcon();
        if (Globals::g_hWindow) DestroyWindow(Globals::g_hWindow);
        UnregisterClass(Config::WINDOW_CLASS_NAME, Globals::g_hInstance);
        CloseHandle(Globals::g_hSpamEvent);
        DeleteCriticalSection(&Globals::g_csActiveKeys);
        // Globals::g_hWindow = NULL;
        Globals::g_hSpamEvent = NULL;
        return false;
    }

    std::cout << "Application Initialization Successful." << std::endl;
    return true;
}

void CleanupApplication() {
    std::cout << "--- Starting Application Cleanup ---" << std::endl;

    StopSpamThread();
    TeardownKeyboardHook();
    RemoveTrayIcon();

    // Window destroyed via message loop sending WM_DESTROY usually
    // Globals::g_hWindow = NULL; // Should be set internally or be invalid

    if (Globals::g_hInstance) {
        if (UnregisterClass(Config::WINDOW_CLASS_NAME, Globals::g_hInstance)) {
            std::cout << "Window class unregistered." << std::endl;
        }
        else {
            // Don't log error if window handle is already invalid (common case)
            if (IsWindow(Globals::g_hWindow)) { // Check if window still exists somehow
                LogError("UnregisterClass failed while window might still exist");
            }
            else {
                // Log if unregister failed for other reasons (less common)
                // LogError("UnregisterClass failed");
                std::cerr << "Warning: UnregisterClass failed (Code: " << GetLastError() << ")" << std::endl;
            }
        }
    }

    if (Globals::g_hSpamEvent) {
        CloseHandle(Globals::g_hSpamEvent);
        Globals::g_hSpamEvent = NULL;
        std::cout << "Spam event handle closed." << std::endl;
    }

    // Assume g_csActiveKeys was initialized if we got this far without crashing.
    DeleteCriticalSection(&Globals::g_csActiveKeys);
    std::cout << "Critical section deleted." << std::endl;

    Globals::g_hInstance = NULL;

    std::cout << "--- Application Cleanup Finished ---" << std::endl;
}