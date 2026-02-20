// Application.cpp
#include "Application.h"
#include "AppWindow.h"
#include "Config.h"
#include "Globals.h"
#include "KeyboardHook.h"
#include "SpamLogic.h"
#include "TrayIcon.h"
#include "Utils.h"
#include <iostream>
#include <map>    // <-- Add
#include <vector> // <-- Add
#include <windows.h>

// --- Define Global Variables (declared extern in Globals.h) ---
namespace Globals {
HHOOK g_hHook = NULL;
HWND g_hWindow = NULL;
HANDLE g_hHookThread = NULL;
HANDLE g_hSpamThread = NULL;
HANDLE g_hSpamEvent = NULL;
HINSTANCE g_hInstance = NULL;

// Need full types here since it's the definition
std::map<int, KeyState> g_KeyInfo;
std::vector<int> g_activeSpamKeys;
std::atomic<unsigned long long> g_spamKeysEpoch{0};
CRITICAL_SECTION g_csActiveKeys; // Initialized in InitializeApplication
std::atomic<bool> g_isCSpamActive{false};

NOTIFYICONDATA g_nid = {0};
} // namespace Globals
// --- End Global Variable Definitions ---

DWORD WINAPI HookThreadFunc(LPVOID lpParam) {
  if (!SetupKeyboardHook(Globals::g_hInstance)) {
    return 1;
  }

  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  TeardownKeyboardHook();
  return 0;
}

bool InitializeApplication(HINSTANCE hInstance) {
  Globals::g_hInstance = hInstance;

  InitializeCriticalSection(&Globals::g_csActiveKeys);

  Config::LoadConfig();

  Globals::g_hSpamEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
  if (!Globals::g_hSpamEvent) {
    LogError("CreateEvent failed");
    DeleteCriticalSection(&Globals::g_csActiveKeys);
    return false;
  }

  // Window creation moved to GuiManager inside main.cpp, but tray icon might
  // still rely on it. We will keep CreateAppWindow as a hidden message window
  // for Tray/Internal events.
  if (!CreateAppWindow(hInstance)) {
    CloseHandle(Globals::g_hSpamEvent);
    DeleteCriticalSection(&Globals::g_csActiveKeys);
    return false;
  }

  Globals::g_KeyInfo['W'];
  Globals::g_KeyInfo['A'];
  Globals::g_KeyInfo['S'];
  Globals::g_KeyInfo['D'];
  int triggerKey = Config::KeySpamTrigger.load();
  if (Globals::g_KeyInfo.find(triggerKey) == Globals::g_KeyInfo.end()) {
    Globals::g_KeyInfo[triggerKey];
  }
  std::cout << "Key states initialized." << std::endl;

  InitNotifyIconData(Globals::g_hWindow);

  Globals::g_hHookThread = CreateThread(NULL, 0, HookThreadFunc, NULL, 0, NULL);
  if (!Globals::g_hHookThread) {
    RemoveTrayIcon();
    if (Globals::g_hWindow)
      DestroyWindow(Globals::g_hWindow);
    UnregisterClass(Config::WINDOW_CLASS_NAME, Globals::g_hInstance);
    CloseHandle(Globals::g_hSpamEvent);
    DeleteCriticalSection(&Globals::g_csActiveKeys);
    Globals::g_hSpamEvent = NULL;
    return false;
  }

  if (!StartSpamThread()) {
    // Need to stop hook thread cleanly
    PostThreadMessage(GetThreadId(Globals::g_hHookThread), WM_QUIT, 0, 0);
    WaitForSingleObject(Globals::g_hHookThread, 2000);
    CloseHandle(Globals::g_hHookThread);
    Globals::g_hHookThread = NULL;

    RemoveTrayIcon();
    if (Globals::g_hWindow)
      DestroyWindow(Globals::g_hWindow);
    UnregisterClass(Config::WINDOW_CLASS_NAME, Globals::g_hInstance);
    CloseHandle(Globals::g_hSpamEvent);
    DeleteCriticalSection(&Globals::g_csActiveKeys);
    Globals::g_hSpamEvent = NULL;
    return false;
  }

  std::cout << "Application Initialization Successful." << std::endl;
  return true;
}

void CleanupApplication() {
  std::cout << "--- Starting Application Cleanup ---" << std::endl;

  StopSpamThread();

  if (Globals::g_hHookThread) {
    PostThreadMessage(GetThreadId(Globals::g_hHookThread), WM_QUIT, 0, 0);
    WaitForSingleObject(Globals::g_hHookThread, 2000);
    CloseHandle(Globals::g_hHookThread);
    Globals::g_hHookThread = NULL;
  }

  RemoveTrayIcon();

  if (Globals::g_hInstance) {
    if (UnregisterClass(Config::WINDOW_CLASS_NAME, Globals::g_hInstance)) {
      std::cout << "Window class unregistered." << std::endl;
    }
  }

  if (Globals::g_hSpamEvent) {
    CloseHandle(Globals::g_hSpamEvent);
    Globals::g_hSpamEvent = NULL;
    std::cout << "Spam event handle closed." << std::endl;
  }

  DeleteCriticalSection(&Globals::g_csActiveKeys);
  std::cout << "Critical section deleted." << std::endl;

  Globals::g_hInstance = NULL;

  std::cout << "--- Application Cleanup Finished ---" << std::endl;
}
