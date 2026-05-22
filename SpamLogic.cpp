// SpamLogic.cpp
#include "SpamLogic.h"
#include "PrecisionTimer.h"
#include "Config.h"
#include "Globals.h"
#include "Application.h"
#include "Logger.h"
#include "Utils.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <timeapi.h>
#include <vector>
#include <windows.h>
#pragma comment(lib, "winmm.lib")

namespace {
std::jthread g_spamThread;
std::mutex g_spamCvMtx;
std::condition_variable g_spamCv;
bool g_spamCvFlag = false;

bool IsPhysicallyHeldMovementKey(int vkCode) {
  if (vkCode != 'W' && vkCode != 'A' && vkCode != 'S' && vkCode != 'D') {
    return false;
  }
  return Globals::g_KeyInfo[vkCode].physicalKeyDown.load(std::memory_order_relaxed);
}

void GetSpamSnapshot(std::vector<int> &keys, unsigned long long &epoch) {
  std::lock_guard<std::mutex> lock(Globals::g_activeKeysMutex);
  keys = Globals::g_activeSpamKeys;
  epoch = Globals::g_spamKeysEpoch.load(std::memory_order_relaxed);
}

void SpamThreadFunc(std::stop_token stopToken) {
  timeBeginPeriod(1);
  PrecisionTimer timer;

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

  while (!stopToken.stop_requested()) {
    DWORD spamDelay = Config::SpamDelayMs.load(std::memory_order_relaxed);
    DWORD waitTimeout = INFINITE;

    bool shouldBeActive =
        Globals::g_isSpamActive.load(std::memory_order_relaxed) &&
        Config::EnableSpam.load(std::memory_order_relaxed);

    if (shouldBeActive) {
      unsigned long long ignoredEpoch = 0;
      GetSpamSnapshot(localActiveKeys, ignoredEpoch);
      const bool keysCurrentlyActive = !localActiveKeys.empty();

      if (keysCurrentlyActive) {
        waitTimeout = ApplyJitter(spamDelay);
      }
    }

    if (waitTimeout == INFINITE) {
      std::unique_lock<std::mutex> cvLock(g_spamCvMtx);
      g_spamCv.wait(cvLock, [&stopToken]() { return g_spamCvFlag || stopToken.stop_requested(); });
      g_spamCvFlag = false;
    } else {
      const LONGLONG start = PrecisionTimer::GetCurrentTicks();
      const LONGLONG targetTick = start + timer.MsToTicks(waitTimeout);
      const LONGLONG spinThreshold = timer.MsToTicks(0.5);

      LONGLONG now;
      do {
        now = PrecisionTimer::GetCurrentTicks();
        if (stopToken.stop_requested())
          break;
        const LONGLONG remaining = targetTick - now;
        if (remaining > spinThreshold) {
          std::unique_lock<std::mutex> cvLock(g_spamCvMtx);
          if (g_spamCv.wait_for(cvLock, std::chrono::milliseconds(1), []() { return g_spamCvFlag; })) {
            g_spamCvFlag = false;
            break;
          }
        } else if (remaining > 0) {
          std::unique_lock<std::mutex> cvLock(g_spamCvMtx);
          if (g_spamCv.wait_for(cvLock, std::chrono::microseconds(0), []() { return g_spamCvFlag; })) {
            g_spamCvFlag = false;
            break;
          }
        }
      } while (now < targetTick);
    }

    if (stopToken.stop_requested()) {
      releaseVirtuallyDownKeys(false);
      break;
    }

    shouldBeActive = Globals::g_isSpamActive.load(std::memory_order_relaxed) &&
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

    DWORD keyDownDuration = ApplyJitter(
        Config::SpamKeyDownDurationMs.load(std::memory_order_relaxed));
    if (keyDownDuration > 0) {
      if (!timer.PreciseSleep(keyDownDuration, stopToken)) {
        releaseVirtuallyDownKeys(false);
        break;
      }
    }

    shouldBeActive = Globals::g_isSpamActive.load(std::memory_order_relaxed) &&
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

  timeEndPeriod(1);
  Logger::GetInstance().Log("Spam thread exiting.");
}
} // namespace

void SendKeyInputBatch(const std::vector<int> &keys, bool keyDown) {
  if (keys.empty())
    return;

  // Send all keys in a single SendInput call so the batch is atomic from the
  // OS's perspective.  The previous one-by-one InjectKey approach allowed the
  // hook callback to interleave between individual keys (e.g. processing an
  // autorepeat W between W-down and D-down), corrupting the expected sequence.
  std::vector<INPUT> inputs(keys.size());
  for (size_t i = 0; i < keys.size(); ++i) {
    inputs[i].type = INPUT_KEYBOARD;
    inputs[i].ki.wVk = 0;
    inputs[i].ki.wScan = VirtualKeyToScanCode(keys[i]);
    inputs[i].ki.dwFlags = VirtualKeyInputFlags(keys[i], keyDown);
    inputs[i].ki.time = 0;
    inputs[i].ki.dwExtraInfo = NEO_SYNTHETIC_INFORMATION;
  }
  SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

void CleanupSpamState(bool restoreHeldKeys) {
  Globals::g_isSpamActive.store(false, std::memory_order_relaxed);

  std::vector<int> keysThatWereSpamming;
  std::vector<int> keysToRelease;
  std::vector<int> physicallyHeldKeys;
  std::vector<int> keysToRestoreDown;
  physicallyHeldKeys.reserve(4);

  {
    std::lock_guard<std::mutex> lock(Globals::g_activeKeysMutex);
    keysThatWereSpamming = Globals::g_activeSpamKeys;
    Globals::g_activeSpamKeys.clear();
    Globals::g_spamKeysEpoch.fetch_add(1ULL, std::memory_order_relaxed);
  }

  for (int key : {'W', 'A', 'S', 'D'}) {
    Globals::KeyState &state = Globals::g_KeyInfo[key];
    state.spamming.store(false, std::memory_order_relaxed);
    const bool physicallyHeld =
        state.physicalKeyDown.load(std::memory_order_relaxed);
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
    Logger::GetInstance().Log("  Sent final UP for spammed keys.");
  }

  if (restoreHeldKeys && !keysToRestoreDown.empty()) {
    SendKeyInputBatch(keysToRestoreDown, true);
    std::string msg = "  Restored DOWN state for physically held keys:";
    for (int k : keysToRestoreDown) {
      msg += ' ';
      msg += static_cast<char>(k);
    }
    Logger::GetInstance().Log(msg);
  }

  TriggerSpamEvent();
}

bool StartSpamThread() {
  StopSpamThread();
  g_spamThread = std::jthread(SpamThreadFunc);
  Logger::GetInstance().Log("Spam thread started.");
  return true;
}

void StopSpamThread() {
  if (g_spamThread.joinable()) {
    Logger::GetInstance().Log("Requesting spam thread stop...");
    g_spamThread.request_stop();
    TriggerSpamEvent();
    g_spamThread.join();
    Logger::GetInstance().Log("Spam thread stopped.");
  }
}

void TriggerSpamEvent() noexcept {
  {
    std::lock_guard<std::mutex> lock(g_spamCvMtx);
    g_spamCvFlag = true;
  }
  g_spamCv.notify_all();
}
