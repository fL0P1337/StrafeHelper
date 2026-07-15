// SpamLogic.cpp
#include "SpamLogic.h"
#include "PrecisionTimer.h"
#include "Config.h"
#include "Globals.h"
#include "Application.h"
#include "Logger.h"
#include "Utils.h"
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <timeapi.h>
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
  return Globals::g_KeyInfo[vkCode].physicalKeyDown.load(std::memory_order_acquire);
}

// Decodes the active spam-key mask from the lurch state atomic into a fixed
// stack array of VK codes, returning the number of keys written and the
// epoch observed at load time.
size_t GetSpamSnapshot(int (&keys)[4], uint32_t &epoch) {
  const uint32_t snapshot =
      Globals::g_lurchState.load(std::memory_order_acquire);
  epoch = snapshot;
  return Globals::DecodeLurchKeys(snapshot & Globals::kLurchKeyMask, keys);
}

void SpamThreadFunc(std::stop_token stopToken) {
  timeBeginPeriod(1);
  PrecisionTimer timer;

  // Stack-allocated buffers — no heap churn per spam cycle.
  int localActiveKeys[4] = {0, 0, 0, 0};
  size_t localActiveCount = 0;
  int virtuallyDownKeys[4] = {0, 0, 0, 0};
  size_t virtuallyDownCount = 0;

  auto releaseVirtuallyDownKeys = [&virtuallyDownKeys, &virtuallyDownCount](
                                      bool preserveHeldKeys) {
    if (virtuallyDownCount == 0) {
      return;
    }

    int keysToRelease[4] = {0, 0, 0, 0};
    size_t keysToReleaseCount = 0;

    for (size_t i = 0; i < virtuallyDownCount; ++i) {
      const int vk = virtuallyDownKeys[i];
      if (preserveHeldKeys && IsPhysicallyHeldMovementKey(vk)) {
        continue;
      }
      keysToRelease[keysToReleaseCount++] = vk;
    }

    if (keysToReleaseCount != 0) {
      SendKeyInputBatch(keysToRelease, keysToReleaseCount, false);
    }

    virtuallyDownCount = 0;
  };

  auto interruptibleSleep = [&timer, &stopToken](DWORD ms) -> bool {
    if (ms == 0) return true;
    const LONGLONG start = PrecisionTimer::GetCurrentTicks();
    const LONGLONG targetTick = start + timer.MsToTicks(ms);
    const LONGLONG spinThreshold = timer.MsToTicks(0.5);

    LONGLONG now = start;
    while (now < targetTick) {
      if (stopToken.stop_requested() || g_spamCvFlag.load(std::memory_order_acquire)) {
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
        Globals::g_isSpamActive.load(std::memory_order_acquire) &&
        Config::EnableSpam.load(std::memory_order_relaxed);

    uint32_t epochBeforeWait = 0;
    localActiveCount = GetSpamSnapshot(localActiveKeys, epochBeforeWait);
    bool keysCurrentlyActive = (localActiveCount != 0);

    if (!shouldBeActive || !keysCurrentlyActive) {
      std::unique_lock<std::mutex> cvLock(g_spamCvMtx);
      g_spamCv.wait(cvLock, [&stopToken]() {
          return g_spamCvFlag.load(std::memory_order_acquire) || stopToken.stop_requested();
      });
      g_spamCvFlag.store(false, std::memory_order_release);
      releaseVirtuallyDownKeys(true);
      continue;
    }

    g_spamCvFlag.store(false, std::memory_order_release);

    // Final pre-inject validation: re-check the lurch state and the spam-
    // active flag right before we touch SendInput. This closes the window
    // where a WASD key was released (or the trigger lifted) between the
    // snapshot above and the actual injection — the previous code would
    // emit one extra ghost DOWN for the just-released key.
    const uint32_t epochNow =
        Globals::g_lurchState.load(std::memory_order_acquire);
    if (epochNow != epochBeforeWait) {
      continue; // Mask changed under us — re-snapshot next iteration.
    }
    if (!Globals::g_isSpamActive.load(std::memory_order_acquire) ||
        !Config::EnableSpam.load(std::memory_order_relaxed)) {
      continue;
    }

    SendKeyInputBatch(localActiveKeys, localActiveCount, true);
    for (size_t i = 0; i < localActiveCount; ++i) {
      virtuallyDownKeys[i] = localActiveKeys[i];
    }
    virtuallyDownCount = localActiveCount;

    DWORD keyDownDuration = ApplyJitter(Config::SpamKeyDownDurationMs.load(std::memory_order_relaxed));
    if (keyDownDuration > 0) {
      if (!interruptibleSleep(keyDownDuration)) {
        bool shouldBeActiveNow = Globals::g_isSpamActive.load(std::memory_order_acquire) &&
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

void SendKeyInputBatch(const int *keys, size_t count, bool keyDown) {
  (void)InjectKeys(keys, count, keyDown);
}

void CleanupSpamState(bool restoreHeldKeys) {
  Globals::g_isSpamActive.store(false, std::memory_order_release);

  // Read the current spam-key bitmask and atomically zero the key bits while
  // bumping the epoch. The publisher CAS in MovementStateManager uses the
  // same epoch convention so cleanup interleaves correctly with publish.
  int keysThatWereSpamming[4] = {0, 0, 0, 0};
  size_t keysThatWereSpammingCount = 0;
  {
    uint32_t prev = Globals::g_lurchState.load(std::memory_order_acquire);
    uint32_t next;
    do {
      const uint32_t epoch =
          (prev & ~Globals::kLurchKeyMask) + Globals::kLurchEpochInc;
      next = epoch & ~Globals::kLurchKeyMask; // key bits cleared
    } while (!Globals::g_lurchState.compare_exchange_weak(
        prev, next, std::memory_order_acq_rel, std::memory_order_acquire));

    keysThatWereSpammingCount = Globals::DecodeLurchKeys(
        prev & Globals::kLurchKeyMask, keysThatWereSpamming);
  }

  int physicallyHeldKeys[4] = {0, 0, 0, 0};
  size_t physicallyHeldCount = 0;
  int keysToRestoreDown[4] = {0, 0, 0, 0};
  size_t keysToRestoreDownCount = 0;

  for (int key : {'W', 'A', 'S', 'D'}) {
    Globals::KeyState &state = Globals::g_KeyInfo[key];
    state.spamming.store(false, std::memory_order_relaxed);
    const bool physicallyHeld =
        state.physicalKeyDown.load(std::memory_order_acquire);
    if (physicallyHeld) {
      physicallyHeldKeys[physicallyHeldCount++] = key;
      if (restoreHeldKeys) {
        keysToRestoreDown[keysToRestoreDownCount++] = key;
      }
    }
  }

  int keysToRelease[4] = {0, 0, 0, 0};
  size_t keysToReleaseCount = 0;
  for (size_t i = 0; i < keysThatWereSpammingCount; ++i) {
    const int vk = keysThatWereSpamming[i];
    bool isStillPhysicallyHeld = false;
    for (size_t j = 0; j < physicallyHeldCount; ++j) {
      if (physicallyHeldKeys[j] == vk) {
        isStillPhysicallyHeld = true;
        break;
      }
    }
    if (!isStillPhysicallyHeld) {
      keysToRelease[keysToReleaseCount++] = vk;
    }
  }

  if (keysToReleaseCount != 0) {
    SendKeyInputBatch(keysToRelease, keysToReleaseCount, false);
    Logger::GetInstance().Log("  Sent final UP for spammed keys.");
  }

  if (restoreHeldKeys && keysToRestoreDownCount != 0) {
    SendKeyInputBatch(keysToRestoreDown, keysToRestoreDownCount, true);
    std::string msg = "  Restored DOWN state for physically held keys:";
    for (size_t i = 0; i < keysToRestoreDownCount; ++i) {
      msg += ' ';
      msg += static_cast<char>(keysToRestoreDown[i]);
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
  g_spamCvFlag.store(true, std::memory_order_release);
  {
    std::lock_guard<std::mutex> lock(g_spamCvMtx);
  }
  g_spamCv.notify_all();
}
