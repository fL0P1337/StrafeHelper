// SuperglideLogic.cpp
//
// Executes the superglide input sequence when triggered:
//   Jump key-down/up -> Sleep(1 frame @ TargetFPS) -> Crouch key-down/up
//
// Frame-time math (from reference: Apex_Superglide_Practice_Tool):
//   frameTime = 1 / targetFPS  (seconds)
//   frameTimeMs = 1000.0 / targetFPS  (milliseconds)
//
// Windows Sleep is accurate to ~1 ms when timeBeginPeriod(1) is active.

#include "SuperglideLogic.h"
#include "PrecisionTimer.h"
#include "Config.h"
#include "globals.h"
#include "Application.h"
#include "Utils.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <timeapi.h> // timeBeginPeriod / timeEndPeriod
#include <windows.h>

#pragma comment(lib, "winmm.lib")

namespace {

// VK codes for the injected sequence (Apex Legends defaults)
constexpr int kJumpVK = VK_SPACE;      // Jump
constexpr int kCrouchVK = VK_LCONTROL; // Crouch (left Ctrl)

std::jthread g_superglideThread;
std::mutex g_superglideCvMtx;
std::condition_variable g_superglideCv;
bool g_superglideCvFlag = false;

// Injects a single key tap (down then up) using KEYEVENTF_SCANCODE.
// Matches the injection style used throughout the rest of the codebase.
static void InjectKeyTap(int vk) noexcept {
  InjectKey(vk, true);
  InjectKey(vk, false);
}

void SuperglideThreadFunc(std::stop_token stopToken) {
  // Raise timer resolution to ~1 ms so Sleep is accurate enough for
  // sub-frame timing at high frame rates.
  timeBeginPeriod(1);
  PrecisionTimer timer;

  while (!stopToken.stop_requested()) {
    {
      std::unique_lock<std::mutex> cvLock(g_superglideCvMtx);
      g_superglideCv.wait(cvLock, [&stopToken]() { return g_superglideCvFlag || stopToken.stop_requested(); });
      g_superglideCvFlag = false;
    }

    if (stopToken.stop_requested())
      break;

    if (!Config::EnableSuperglide.load(std::memory_order_relaxed))
      continue;

    const double targetFPS = Config::TargetFPS.load(std::memory_order_relaxed);
    const double frameTimeMs = 1000.0 / targetFPS;

    // 1. Jump tap — record timing immediately after injection so the
    //    measurement baseline matches the practice tool.
    InjectKeyTap(kJumpVK);

    const LONGLONG start = PrecisionTimer::GetCurrentTicks();
    const LONGLONG targetTick = start + timer.MsToTicks(frameTimeMs);

    // 2. Hybrid wait: coarse Sleep(1) releases the CPU for most of the frame,
    //    then we spin on QPC for the final ~0.5 ms for precision.
    if (!timer.PreciseSleepUntil(targetTick, stopToken)) {
      break;
    }

    const LONGLONG now = PrecisionTimer::GetCurrentTicks();

    // 3. Crouch tap — fires at exactly 1 frame after Jump.
    InjectKeyTap(kCrouchVK);

    // Record timing stats using the same formula as the practice tool:
    //   elapsedFrames = elapsed_seconds / frameTime
    //   if < 1 frame: chance = elapsedFrames * 100  (too early)
    //   if < 2 frames: chance = (2 - elapsedFrames) * 100  (slightly late)
    //   else: 0  (way too late)
    {
      const double elapsedTicks = static_cast<double>(now - start);
      const double elapsedSec = elapsedTicks / static_cast<double>(timer.GetFrequency());
      const double frameTimeSec = 1.0 / targetFPS;
      const double elapsedFrames = elapsedSec / frameTimeSec;

      double chance = 0.0;
      if (elapsedFrames < 1.0)
        chance = elapsedFrames * 100.0;
      else if (elapsedFrames < 2.0)
        chance = (2.0 - elapsedFrames) * 100.0;

      const double errorMs = (elapsedSec - frameTimeSec) * 1000.0;

      auto &stats = Globals::g_superglideStats;
      const int idx =
          stats.writeIdx.fetch_add(1, std::memory_order_relaxed) %
          Globals::kSuperglideHistorySize;
      stats.history[idx] = {elapsedFrames, chance, errorMs};
      stats.count.fetch_add(1, std::memory_order_relaxed);
    }

    // Cooldown/delay to prevent double-activation
    {
      std::unique_lock<std::mutex> cvLock(g_superglideCvMtx);
      g_superglideCv.wait_for(cvLock, std::chrono::milliseconds(500), [&stopToken]() {
        return stopToken.stop_requested();
      });
    }
  }

  timeEndPeriod(1);
  std::cout << "Superglide thread exiting." << std::endl;
}

} // namespace

// ---- Public API ----

bool StartSuperglideThread() {
  StopSuperglideThread();
  g_superglideThread = std::jthread(SuperglideThreadFunc);
  std::cout << "Superglide thread started." << std::endl;
  return true;
}

void StopSuperglideThread() {
  if (g_superglideThread.joinable()) {
    std::cout << "Requesting Superglide thread stop..." << std::endl;
    g_superglideThread.request_stop();
    TriggerSuperglide();
    g_superglideThread.join();
    std::cout << "Superglide thread stopped." << std::endl;
  }
}

void TriggerSuperglide() noexcept {
  {
    std::lock_guard<std::mutex> lock(g_superglideCvMtx);
    g_superglideCvFlag = true;
  }
  g_superglideCv.notify_all();
}
