// TurboLogic.cpp
#include "TurboLogic.h"
#include "Config.h"
#include "Globals.h"
#include "Utils.h"
#include <atomic>
#include <iostream>
#include <windows.h>

namespace {

// ---- Turbo Loot ----
std::atomic<bool> g_stopTurboLootRequest{false};

DWORD WINAPI TurboLootThread(LPVOID) {
  while (!g_stopTurboLootRequest.load(std::memory_order_relaxed)) {
    DWORD timeout = INFINITE;

    const bool enabled =
        Config::EnableTurboLoot.load(std::memory_order_relaxed);
    const int vk = Config::TurboLootKey.load(std::memory_order_relaxed);

    if (enabled) {
      auto it = Globals::g_KeyInfo.find(vk);
      if (it != Globals::g_KeyInfo.end() &&
          it->second.physicalKeyDown.load(std::memory_order_relaxed)) {
        timeout = Config::TurboLootDelayMs.load(std::memory_order_relaxed);
      }
    }

    WaitForSingleObject(Globals::g_hTurboLootEvent, timeout);

    if (g_stopTurboLootRequest.load(std::memory_order_relaxed))
      break;

    if (!Config::EnableTurboLoot.load(std::memory_order_relaxed))
      continue;

    const int key = Config::TurboLootKey.load(std::memory_order_relaxed);
    auto it = Globals::g_KeyInfo.find(key);
    if (it == Globals::g_KeyInfo.end() ||
        !it->second.physicalKeyDown.load(std::memory_order_relaxed))
      continue;

    // Send key-down
    INPUT input = {INPUT_KEYBOARD};
    input.ki.wVk = key;
    input.ki.wScan = MapVirtualKey(key, MAPVK_VK_TO_VSC);
    input.ki.dwFlags = KEYEVENTF_SCANCODE;
    input.ki.dwExtraInfo = GetMessageExtraInfo();
    SendInput(1, &input, sizeof(INPUT));

    DWORD duration =
        Config::TurboLootDurationMs.load(std::memory_order_relaxed);
    if (duration > 0) {
      Sleep(duration);
      if (g_stopTurboLootRequest.load(std::memory_order_relaxed))
        break;
    }

    // Send key-up
    input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
  }

  std::cout << "TurboLoot thread exiting." << std::endl;
  return 0;
}

// ---- Turbo Jump ----
std::atomic<bool> g_stopTurboJumpRequest{false};

DWORD WINAPI TurboJumpThread(LPVOID) {
  while (!g_stopTurboJumpRequest.load(std::memory_order_relaxed)) {
    DWORD timeout = INFINITE;

    const bool enabled =
        Config::EnableTurboJump.load(std::memory_order_relaxed);
    const int vk = Config::TurboJumpKey.load(std::memory_order_relaxed);

    if (enabled) {
      auto it = Globals::g_KeyInfo.find(vk);
      if (it != Globals::g_KeyInfo.end() &&
          it->second.physicalKeyDown.load(std::memory_order_relaxed)) {
        timeout = Config::TurboJumpDelayMs.load(std::memory_order_relaxed);
      }
    }

    WaitForSingleObject(Globals::g_hTurboJumpEvent, timeout);

    if (g_stopTurboJumpRequest.load(std::memory_order_relaxed))
      break;

    if (!Config::EnableTurboJump.load(std::memory_order_relaxed))
      continue;

    const int key = Config::TurboJumpKey.load(std::memory_order_relaxed);
    auto it = Globals::g_KeyInfo.find(key);
    if (it == Globals::g_KeyInfo.end() ||
        !it->second.physicalKeyDown.load(std::memory_order_relaxed))
      continue;

    // Send key-down
    INPUT input = {INPUT_KEYBOARD};
    input.ki.wVk = key;
    input.ki.wScan = MapVirtualKey(key, MAPVK_VK_TO_VSC);
    input.ki.dwFlags = KEYEVENTF_SCANCODE;
    input.ki.dwExtraInfo = GetMessageExtraInfo();
    SendInput(1, &input, sizeof(INPUT));

    DWORD duration =
        Config::TurboJumpDurationMs.load(std::memory_order_relaxed);
    if (duration > 0) {
      Sleep(duration);
      if (g_stopTurboJumpRequest.load(std::memory_order_relaxed))
        break;
    }

    // Send key-up
    input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
  }

  std::cout << "TurboJump thread exiting." << std::endl;
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
