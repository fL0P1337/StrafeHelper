// SpamLogic.cpp
#include "SpamLogic.h"
#include "Config.h"
#include "Globals.h" // <-- Add
#include "Utils.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include <windows.h> // <-- Add

namespace {
std::atomic<bool> g_stopSpamThreadRequest = false;

void GetSpamSnapshot(std::vector<int> &keys, unsigned long long &epoch) {
  EnterCriticalSection(&Globals::g_csActiveKeys);
  keys = Globals::g_activeSpamKeys;
  epoch = Globals::g_spamKeysEpoch.load(std::memory_order_relaxed);
  LeaveCriticalSection(&Globals::g_csActiveKeys);
}
} // namespace

DWORD WINAPI SpamThread(LPVOID lpParam) {
  std::vector<int> desiredKeys;
  std::vector<int> keysCurrentlyDown;
  desiredKeys.reserve(8);
  keysCurrentlyDown.reserve(8);

  while (!g_stopSpamThreadRequest.load(std::memory_order_relaxed)) {
    DWORD spamDelay = Config::SpamDelayMs.load(std::memory_order_relaxed);
    DWORD keyDownDuration =
        Config::SpamKeyDownDurationMs.load(std::memory_order_relaxed);

    bool shouldBeActive =
        Globals::g_isCSpamActive.load(std::memory_order_relaxed) &&
        Config::EnableSpam.load(std::memory_order_relaxed);

    unsigned long long epoch = 0;

    if (shouldBeActive) {
      GetSpamSnapshot(desiredKeys, epoch);
    } else {
      // Spam deactivated (e.g., C released).
      // The main thread's CleanupSpamState has ALREADY handled sending the
      // final KEY_UP and restoring physically held keys with KEY_DOWN. We MUST
      // NOT send KEY_UP here, or we will destroy the newly restored KEY_DOWN
      // state!
      desiredKeys.clear();
      keysCurrentlyDown.clear();
      WaitForSingleObject(Globals::g_hSpamEvent, INFINITE);
      continue;
    }

    // Release any keys that are no longer in desiredKeys (e.g., user released W
    // mid-spam)
    std::vector<int> keysToRelease;
    for (auto it = keysCurrentlyDown.begin(); it != keysCurrentlyDown.end();) {
      int pressed_key = *it;
      if (std::find(desiredKeys.begin(), desiredKeys.end(), pressed_key) ==
          desiredKeys.end()) {
        keysToRelease.push_back(pressed_key);
        it = keysCurrentlyDown.erase(it);
      } else {
        ++it;
      }
    }

    if (!keysToRelease.empty()) {
      SendKeyInputBatch(keysToRelease, false);
    }

    // Wait for spamDelay IF we just released keys (or are looping),
    // EXCEPT if we woke up from INFINITE and are starting completely fresh.
    // We use waitTimeout = spamDelay if we are actively spamming.
    // If waitTimeout == 0, we skip the Phase 1 delay.
    DWORD waitRes = WaitForSingleObject(
        Globals::g_hSpamEvent, keysCurrentlyDown.empty() ? 0 : spamDelay);
    if (g_stopSpamThreadRequest.load(std::memory_order_relaxed))
      break;

    // At this point, whether we timed out (natural delay) or were signaled (new
    // keys/trigger), we re-evaluate if we should press keys DOWN.

    // Re-check activation and snapshot before pressing keys DOWN
    shouldBeActive = Globals::g_isCSpamActive.load(std::memory_order_relaxed) &&
                     Config::EnableSpam.load(std::memory_order_relaxed);
    if (!shouldBeActive)
      continue;

    GetSpamSnapshot(desiredKeys, epoch);
    if (desiredKeys.empty())
      continue;

    // Phase 2: Press desiredKeys DOWN and wait keyDownDuration
    SendKeyInputBatch(desiredKeys, true);
    keysCurrentlyDown = desiredKeys;

    waitRes = WaitForSingleObject(Globals::g_hSpamEvent, keyDownDuration);
    if (g_stopSpamThreadRequest.load(std::memory_order_relaxed))
      break;

    // At the end of Phase 2 (whether timed out or signaled), loop back to top.
    // The top of the loop will handle sending them UP for Phase 1.
    // To fix this, we ALWAYS release all currentlyDown keys at the end of the
    // duration so they are UP during spamDelay.
    SendKeyInputBatch(keysCurrentlyDown, false);
    keysCurrentlyDown.clear();
  }

  if (!keysCurrentlyDown.empty()) {
    SendKeyInputBatch(keysCurrentlyDown, false);
  }

  std::cout << "Spam thread exiting." << std::endl;
  return 0;
}

void SendKeyInputBatch(const std::vector<int> &keys, bool keyDown) {
  if (keys.empty())
    return;

  std::vector<INPUT> inputs(keys.size());
  for (size_t i = 0; i < keys.size(); ++i) {
    inputs[i].type = INPUT_KEYBOARD;
    inputs[i].ki.wVk = keys[i];
    inputs[i].ki.wScan = MapVirtualKey(keys[i], MAPVK_VK_TO_VSC);
    inputs[i].ki.dwFlags = KEYEVENTF_SCANCODE | (keyDown ? 0 : KEYEVENTF_KEYUP);
    inputs[i].ki.time = 0;
    inputs[i].ki.dwExtraInfo = GetMessageExtraInfo();
  }
  UINT sent =
      SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
  if (sent != inputs.size()) {
    std::cerr << "Warning: SendInput sent " << sent << "/" << inputs.size()
              << " events. Error: " << GetLastError() << std::endl;
  }
}

void CleanupSpamState(bool restoreHeldKeys) {
  Globals::g_isCSpamActive.store(false, std::memory_order_relaxed); // Globals::

  std::vector<int> keysThatWereSpamming;
  std::vector<int> keysToRestoreDown;

  EnterCriticalSection(&Globals::g_csActiveKeys);   // Globals::
  keysThatWereSpamming = Globals::g_activeSpamKeys; // Globals::
  Globals::g_activeSpamKeys.clear();                // Globals::
  Globals::g_spamKeysEpoch.fetch_add(1ULL, std::memory_order_relaxed);
  LeaveCriticalSection(&Globals::g_csActiveKeys); // Globals::

  for (int key : {'W', 'A', 'S', 'D'}) {
    // --- Re-add KeyState& declaration ---
    Globals::KeyState &state = Globals::g_KeyInfo[key]; // Globals:: (both)
    // ---
    state.spamming.store(false, std::memory_order_relaxed); // Use variable
    if (restoreHeldKeys &&
        state.physicalKeyDown.load(std::memory_order_relaxed)) { // Use variable
      keysToRestoreDown.push_back(key);
    }
  }

  if (!keysThatWereSpamming.empty()) {
    SendKeyInputBatch(keysThatWereSpamming, false);
    std::cout << "  Sent final UP for spammed keys." << std::endl;
  }

  if (restoreHeldKeys && !keysToRestoreDown.empty()) {
    Sleep(5);
    SendKeyInputBatch(keysToRestoreDown, true);
    std::cout << "  Restored DOWN state for physically held keys: ";
    for (int k : keysToRestoreDown)
      std::cout << static_cast<char>(k) << " ";
    std::cout << std::endl;
  }

  if (Globals::g_hSpamEvent) {       // Globals::
    SetEvent(Globals::g_hSpamEvent); // Globals::
  }
}

bool StartSpamThread() {
  g_stopSpamThreadRequest.store(false);
  // Use Globals:: prefix
  Globals::g_hSpamThread = CreateThread(NULL, 0, SpamThread, NULL, 0, NULL);
  if (!Globals::g_hSpamThread) { // Globals::
    LogError("CreateThread for SpamThread failed");
    return false;
  }
  std::cout << "Spam thread started." << std::endl;
  return true;
}

void StopSpamThread() {
  if (Globals::g_hSpamThread) { // Globals::
    std::cout << "Requesting spam thread stop..." << std::endl;
    g_stopSpamThreadRequest.store(true);

    if (Globals::g_hSpamEvent) {       // Globals::
      SetEvent(Globals::g_hSpamEvent); // Globals::
    }

    WaitForSingleObject(Globals::g_hSpamThread, 1000); // Globals::

    CloseHandle(Globals::g_hSpamThread); // Globals::
    Globals::g_hSpamThread = NULL;       // Globals::
    std::cout << "Spam thread stopped and handle closed." << std::endl;
  }
  g_stopSpamThreadRequest.store(false);
}
