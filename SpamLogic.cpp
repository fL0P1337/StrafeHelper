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
#include <atomic>
#pragma comment(lib, "winmm.lib")

namespace {
std::jthread g_spamThread;
std::mutex g_spamCvMtx;
std::condition_variable g_spamCv;
std::atomic<bool> g_spamCvFlag{false};

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
        continue;
      }
      keysToRelease.push_back(vk);
    }

    if (!keysToRelease.empty()) {
      SendKeyInputBatch(keysToRelease, false);
    }

    virtuallyDownKeys.clear();
  };

  auto interruptibleSleep = [&timer, &stopToken](DWORD ms) -> bool {
    if (ms == 0) return true;
    const LONGLONG start = PrecisionTimer::GetCurrentTicks();
    const LONGLONG targetTick = start + timer.MsToTicks(ms);
    const LONGLONG spinThreshold = timer.MsToTicks(0.5);

    LONGLONG now = start;
    while (now < targetTick) {
      if (stopToken.stop_requested() || g_spamCvFlag.load(std::memory_order_relaxed)) {
        return false;
      }
      const LONGLONG remaining = targetTick - now;
      if (remaining > spinThreshold) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      } else {
        YieldProcessor();
      }
      now = PrecisionTimer::GetCurrentTicks();
    }
    return true;
  };

  while (!stopToken.stop_requested()) {
    bool shouldBeActive =
        Globals::g_isSpamActive.load(std::memory_order_relaxed) &&
        Config::EnableSpam.load(std::memory_order_relaxed);

    unsigned long long epochBeforeWait = 0;
    GetSpamSnapshot(localActiveKeys, epochBeforeWait);
    bool keysCurrentlyActive = !localActiveKeys.empty();

    if (!shouldBeActive || !keysCurrentlyActive) {
      std::unique_lock<std::mutex> cvLock(g_spamCvMtx);
      g_spamCv.wait(cvLock, [&stopToken]() { 
          return g_spamCvFlag.load(std::memory_order_relaxed) || stopToken.stop_requested(); 
      });
      g_spamCvFlag.store(false, std::memory_order_relaxed);
      releaseVirtuallyDownKeys(true);
      continue;
    }

    g_spamCvFlag.store(false, std::memory_order_relaxed);

    if (Globals::g_spamKeysEpoch.load(std::memory_order_relaxed) != epochBeforeWait) {
      continue;
    }

    SendKeyInputBatch(localActiveKeys, true);
    virtuallyDownKeys = localActiveKeys;

    DWORD keyDownDuration = ApplyJitter(Config::SpamKeyDownDurationMs.load(std::memory_order_relaxed));
    if (keyDownDuration > 0) {
      if (!interruptibleSleep(keyDownDuration)) {
        bool shouldBeActiveNow = Globals::g_isSpamActive.load(std::memory_order_relaxed) &&
                                 Config::EnableSpam.load(std::memory_order_relaxed);
        releaseVirtuallyDownKeys(!shouldBeActiveNow);
        continue;
      }
    }

    if (stopToken.stop_requested()) {
      releaseVirtuallyDownKeys(false);
      break;
    }

    releaseVirtuallyDownKeys(false);

    DWORD spamDelay = ApplyJitter(Config::SpamDelayMs.load(std::memory_order_relaxed));
    if (spamDelay > 0) {
      if (!interruptibleSleep(spamDelay)) {
        continue;
      }
    }
  }

  releaseVirtuallyDownKeys(false);
  timeEndPeriod(1);
  Logger::GetInstance().Log("Spam thread exiting.");
}
} // namespace

void SendKeyInputBatch(const std::vector<int> &keys, bool keyDown) {
  if (keys.empty())
    return;

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
  g_spamCvFlag.store(true, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(g_spamCvMtx);
  }
  g_spamCv.notify_all();
}
