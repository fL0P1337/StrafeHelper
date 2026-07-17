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
#include "Globals.h"
#include "Application.h"
#include "Logger.h"
#include "Utils.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <system_error>
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
std::atomic<SuperglideExecutionState> g_executionState{
    SuperglideExecutionState::Idle};
std::atomic<ULONGLONG> g_cooldownEndTick{0};

// Injects a single key tap (down then up).
static bool InjectKeyTap(int vk) noexcept {
  if (!InjectKey(vk, true)) {
    return false;
  }
  if (!InjectKey(vk, false)) {
    (void)InjectKey(vk, false);
    return false;
  }
  return true;
}

// Generic stop helper shared with TurboLogic pattern.
template <typename TriggerFn>
void StopWorkerThread(std::jthread &thread, TriggerFn trigger,
                      const char *name) {
  if (thread.joinable()) {
    Logger::GetInstance().Log(std::string("Requesting ") + name + " thread stop...");
    thread.request_stop();
    trigger();
    thread.join();
    Logger::GetInstance().Log(std::string(name) + " thread stopped.");
  }
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

    g_executionState.store(SuperglideExecutionState::Executing,
                           std::memory_order_release);

    // 1. Jump tap — record timing immediately after injection so the
    //    measurement baseline matches the practice tool.
    if (!InjectKeyTap(kJumpVK)) {
      g_executionState.store(SuperglideExecutionState::Idle,
                             std::memory_order_release);
      continue;
    }

    const LONGLONG start = PrecisionTimer::GetCurrentTicks();
    const LONGLONG targetTick = start + timer.MsToTicks(frameTimeMs);

    // 2. Hybrid wait: coarse Sleep(1) releases the CPU for most of the frame,
    //    then we spin on QPC for the final ~0.5 ms for precision.
    if (!timer.PreciseSleepUntil(targetTick, stopToken)) {
      break;
    }

    // 3. Crouch tap — fires at exactly 1 frame after Jump.
    if (!InjectKeyTap(kCrouchVK)) {
      g_executionState.store(SuperglideExecutionState::Idle,
                             std::memory_order_release);
      continue;
    }
    const LONGLONG now = PrecisionTimer::GetCurrentTicks();

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
      std::lock_guard<std::mutex> lock(stats.mutex);
      const int idx = stats.writeIdx.load(std::memory_order_relaxed) %
                      Globals::kSuperglideHistorySize;
      stats.history[idx] = {elapsedFrames, chance, errorMs};
      stats.writeIdx.store(idx + 1, std::memory_order_relaxed);
      stats.count.fetch_add(1, std::memory_order_relaxed);
    }

    // Cooldown/delay to prevent double-activation
    g_executionState.store(SuperglideExecutionState::Cooldown,
                           std::memory_order_release);
    g_cooldownEndTick.store(GetTickCount64() + 500,
                            std::memory_order_release);
    {
      std::unique_lock<std::mutex> cvLock(g_superglideCvMtx);
      g_superglideCv.wait_for(cvLock, std::chrono::milliseconds(500), [&stopToken]() {
        return stopToken.stop_requested();
      });
    }
    g_cooldownEndTick.store(0, std::memory_order_release);
    g_executionState.store(SuperglideExecutionState::Idle,
                           std::memory_order_release);
  }

  g_cooldownEndTick.store(0, std::memory_order_release);
  g_executionState.store(SuperglideExecutionState::Idle,
                         std::memory_order_release);
  timeEndPeriod(1);
  Logger::GetInstance().Log("Superglide thread exiting.");
}

} // namespace

// ---- Public API ----

bool StartSuperglideThread() {
  StopSuperglideThread();
  try {
    g_superglideThread = std::jthread(SuperglideThreadFunc);
  } catch (const std::system_error &e) {
    Logger::GetInstance().Log(std::string("Failed to start Superglide thread: ") +
                              e.what());
    return false;
  }
  Logger::GetInstance().Log("Superglide thread started.");
  return true;
}

void StopSuperglideThread() {
  StopWorkerThread(g_superglideThread, TriggerSuperglide, "Superglide");
}

void TriggerSuperglide() noexcept {
  if (g_executionState.load(std::memory_order_acquire) !=
      SuperglideExecutionState::Idle) {
    g_superglideCv.notify_all();
    return;
  }
  {
    std::lock_guard<std::mutex> lock(g_superglideCvMtx);
    g_superglideCvFlag = true;
  }
  g_superglideCv.notify_all();
}

SuperglideExecutionState GetSuperglideExecutionState() noexcept {
  return g_executionState.load(std::memory_order_acquire);
}

unsigned long GetSuperglideCooldownRemainingMs() noexcept {
  const ULONGLONG end = g_cooldownEndTick.load(std::memory_order_acquire);
  const ULONGLONG now = GetTickCount64();
  if (end <= now) {
    return 0;
  }
  const ULONGLONG remaining = end - now;
  return static_cast<unsigned long>(std::min<ULONGLONG>(remaining, 500));
}
