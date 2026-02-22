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

bool IsPhysicallyHeldMovementKey(int vkCode) {
  if (vkCode != 'W' && vkCode != 'A' && vkCode != 'S' && vkCode != 'D') {
    return false;
  }

  const auto it = Globals::g_KeyInfo.find(vkCode);
  if (it == Globals::g_KeyInfo.end()) {
    return false;
  }

  return it->second.physicalKeyDown.load(std::memory_order_relaxed);
}

void GetSpamSnapshot(std::vector<int> &keys, unsigned long long &epoch) {
  EnterCriticalSection(&Globals::g_csActiveKeys);
  keys = Globals::g_activeSpamKeys;
  epoch = Globals::g_spamKeysEpoch.load(std::memory_order_relaxed);
  LeaveCriticalSection(&Globals::g_csActiveKeys);
}
} // namespace

DWORD WINAPI SpamThread(LPVOID lpParam) {
  std::vector<int> localActiveKeys;
  localActiveKeys.reserve(8);
  std::vector<int> virtuallyDownKeys;
  virtuallyDownKeys.reserve(8);

  auto releaseVirtuallyDownKeys = [&virtuallyDownKeys](bool preserveHeldKeys) {
    if (virtuallyDownKeys.empty()) {
      return;
    }

    std::vector<int> keysToRelease;
    keysToRelease.reserve(virtuallyDownKeys.size());

    for (int vk : virtuallyDownKeys) {
      if (preserveHeldKeys && IsPhysicallyHeldMovementKey(vk)) {
        // Transfer ownership to default handling without a forced key-up.
        continue;
      }
      keysToRelease.push_back(vk);
    }

    if (!keysToRelease.empty()) {
      SendKeyInputBatch(keysToRelease, false);
    }

    virtuallyDownKeys.clear();
  };

  while (!g_stopSpamThreadRequest.load(std::memory_order_relaxed)) {
    DWORD spamDelay = Config::SpamDelayMs.load(std::memory_order_relaxed);
    DWORD waitTimeout = INFINITE;

    bool shouldBeActive =
        Globals::g_isCSpamActive.load(std::memory_order_relaxed) &&
        Config::EnableSpam.load(std::memory_order_relaxed);

    if (shouldBeActive) {
      unsigned long long ignoredEpoch = 0;
      GetSpamSnapshot(localActiveKeys, ignoredEpoch);
      const bool keysCurrentlyActive = !localActiveKeys.empty();

      if (keysCurrentlyActive) {
        waitTimeout = spamDelay;
      }
    }

    WaitForSingleObject(Globals::g_hSpamEvent, waitTimeout);

    if (g_stopSpamThreadRequest.load(std::memory_order_relaxed)) {
      releaseVirtuallyDownKeys(false);
      break;
    }

    shouldBeActive = Globals::g_isCSpamActive.load(std::memory_order_relaxed) &&
                     Config::EnableSpam.load(std::memory_order_relaxed);

    if (!shouldBeActive) {
      releaseVirtuallyDownKeys(true);
      continue;
    }

    // Always release keys that this thread previously drove down.
    releaseVirtuallyDownKeys(false);

    unsigned long long ignoredEpoch = 0;
    GetSpamSnapshot(localActiveKeys, ignoredEpoch);
    if (localActiveKeys.empty()) {
      continue;
    }

    DWORD keyDownDuration =
        Config::SpamKeyDownDurationMs.load(std::memory_order_relaxed);
    if (keyDownDuration > 0) {
      Sleep(keyDownDuration);
      if (g_stopSpamThreadRequest.load(std::memory_order_relaxed)) {
        releaseVirtuallyDownKeys(false);
        break;
      }
    }

    shouldBeActive = Globals::g_isCSpamActive.load(std::memory_order_relaxed) &&
                     Config::EnableSpam.load(std::memory_order_relaxed);
    if (!shouldBeActive) {
      continue;
    }

    unsigned long long epochBeforeDown = 0;
    GetSpamSnapshot(localActiveKeys, epochBeforeDown);

    if (localActiveKeys.empty()) {
      continue;
    }

    if (Globals::g_spamKeysEpoch.load(std::memory_order_relaxed) !=
        epochBeforeDown) {
      continue;
    }

    SendKeyInputBatch(localActiveKeys, true);
    virtuallyDownKeys = localActiveKeys;
  }

  releaseVirtuallyDownKeys(false);

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
  std::vector<int> keysToRelease;
  std::vector<int> physicallyHeldKeys;
  std::vector<int> keysToRestoreDown;
  physicallyHeldKeys.reserve(4);

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
    const bool physicallyHeld = state.physicalKeyDown.load(std::memory_order_relaxed);
    if (physicallyHeld) {
      physicallyHeldKeys.push_back(key);
      if (restoreHeldKeys) {
        keysToRestoreDown.push_back(key);
      }
    }
  }

  keysToRelease.reserve(keysThatWereSpamming.size());
  for (int vk : keysThatWereSpamming) {
    const bool isStillPhysicallyHeld =
        std::find(physicallyHeldKeys.begin(), physicallyHeldKeys.end(), vk) !=
        physicallyHeldKeys.end();
    if (!isStillPhysicallyHeld) {
      keysToRelease.push_back(vk);
    }
  }

  if (!keysToRelease.empty()) {
    SendKeyInputBatch(keysToRelease, false);
    std::cout << "  Sent final UP for spammed keys." << std::endl;
  }

  if (restoreHeldKeys && !keysToRestoreDown.empty()) {
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
