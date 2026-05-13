// TurboLogic.cpp
#include "TurboLogic.h"
#include "Config.h"
#include "Globals.h"
#include "KeybindManager.h"
#include "Utils.h"
#include <atomic>
#include <iostream>
#include <random>
#include <windows.h>

namespace {

void RunTurboLoop(std::atomic<bool> &stopRequest, HANDLE eventHandle,
                  std::atomic<bool> &configEnable,
                  std::atomic<int> &configKey, std::atomic<int> &configDelay,
                  std::atomic<int> &configDuration, bool (*isActiveFunc)(),
                  const char *threadName) {
  while (!stopRequest.load(std::memory_order_relaxed)) {
    DWORD timeout = INFINITE;

    if (configEnable.load(std::memory_order_relaxed)) {
      if (isActiveFunc()) {
        timeout = ApplyJitter(configDelay.load(std::memory_order_relaxed));
      }
    }

    WaitForSingleObject(eventHandle, timeout);

    if (stopRequest.load(std::memory_order_relaxed))
      break;

    if (!configEnable.load(std::memory_order_relaxed))
      continue;

    if (!isActiveFunc())
      continue;

    const int key = configKey.load(std::memory_order_relaxed);

    INPUT input = {INPUT_KEYBOARD};
    input.ki.wVk = key;
    input.ki.wScan = MapVirtualKey(key, MAPVK_VK_TO_VSC);
    input.ki.dwFlags = KEYEVENTF_SCANCODE;
    input.ki.dwExtraInfo = GetMessageExtraInfo();
    SendInput(1, &input, sizeof(INPUT));

    DWORD duration =
        ApplyJitter(configDuration.load(std::memory_order_relaxed));
    if (duration > 0) {
      Sleep(duration);
      if (stopRequest.load(std::memory_order_relaxed))
        break;
    }

    // Send key-up
    input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
  }

  std::cout << threadName << " thread exiting." << std::endl;
}

// ---- Turbo Loot ----
std::atomic<bool> g_stopTurboLootRequest{false};

DWORD WINAPI TurboLootThread(LPVOID) {
  RunTurboLoop(g_stopTurboLootRequest, Globals::g_hTurboLootEvent,
               Config::EnableTurboLoot, Config::TurboLootKey,
               Config::TurboLootDelayMs, Config::TurboLootDurationMs,
               KeybindManager::IsTurboLootActive, "TurboLoot");
  return 0;
}

// ---- Turbo Jump ----
std::atomic<bool> g_stopTurboJumpRequest{false};

DWORD WINAPI TurboJumpThread(LPVOID) {
  RunTurboLoop(g_stopTurboJumpRequest, Globals::g_hTurboJumpEvent,
               Config::EnableTurboJump, Config::TurboJumpKey,
               Config::TurboJumpDelayMs, Config::TurboJumpDurationMs,
               KeybindManager::IsTurboJumpActive, "TurboJump");
  return 0;
}

} // namespace

// ---- Public API ----

bool StartTurboLootThread() {
  g_stopTurboLootRequest.store(false);
  Globals::g_hTurboLootThread =
      CreateThread(NULL, 0, TurboLootThread, NULL, 0, NULL);
  if (!Globals::g_hTurboLootThread) {
    LogError("CreateThread for TurboLootThread failed");
    return false;
  }
  std::cout << "TurboLoot thread started." << std::endl;
  return true;
}

void StopTurboLootThread() {
  if (Globals::g_hTurboLootThread) {
    std::cout << "Requesting TurboLoot thread stop..." << std::endl;
    g_stopTurboLootRequest.store(true);
    if (Globals::g_hTurboLootEvent)
      SetEvent(Globals::g_hTurboLootEvent);
    WaitForSingleObject(Globals::g_hTurboLootThread, 1000);
    CloseHandle(Globals::g_hTurboLootThread);
    Globals::g_hTurboLootThread = NULL;
    std::cout << "TurboLoot thread stopped." << std::endl;
  }
  g_stopTurboLootRequest.store(false);
}

bool StartTurboJumpThread() {
  g_stopTurboJumpRequest.store(false);
  Globals::g_hTurboJumpThread =
      CreateThread(NULL, 0, TurboJumpThread, NULL, 0, NULL);
  if (!Globals::g_hTurboJumpThread) {
    LogError("CreateThread for TurboJumpThread failed");
    return false;
  }
  std::cout << "TurboJump thread started." << std::endl;
  return true;
}

void StopTurboJumpThread() {
  if (Globals::g_hTurboJumpThread) {
    std::cout << "Requesting TurboJump thread stop..." << std::endl;
    g_stopTurboJumpRequest.store(true);
    if (Globals::g_hTurboJumpEvent)
      SetEvent(Globals::g_hTurboJumpEvent);
    WaitForSingleObject(Globals::g_hTurboJumpThread, 1000);
    CloseHandle(Globals::g_hTurboJumpThread);
    Globals::g_hTurboJumpThread = NULL;
    std::cout << "TurboJump thread stopped." << std::endl;
  }
  g_stopTurboJumpRequest.store(false);
}
