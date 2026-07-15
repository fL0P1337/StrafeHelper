// TurboLogic.cpp
#include "TurboLogic.h"
#include "PrecisionTimer.h"
#include "Config.h"
#include "Globals.h"
#include "Application.h"
#include "KeybindManager.h"
#include "Logger.h"
#include "Utils.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <timeapi.h>
#include <windows.h>

#pragma comment(lib, "winmm.lib")

namespace {

std::jthread g_turboLootThread;
std::mutex g_turboLootCvMtx;
std::condition_variable g_turboLootCv;
bool g_turboLootCvFlag = false;

std::jthread g_turboJumpThread;
std::mutex g_turboJumpCvMtx;
std::condition_variable g_turboJumpCv;
bool g_turboJumpCvFlag = false;

// -----------------------------------------------------------------------
// Generic turbo loop — handles both TurboLoot and TurboJump.
// -----------------------------------------------------------------------
void RunTurboLoop(std::stop_token stopToken,
                  std::mutex &cvMtx, std::condition_variable &cv, bool &cvFlag,
                  std::atomic<bool> &configEnable, std::atomic<int> &configKey,
                  std::atomic<int> &configDelay,
                  std::atomic<int> &configDuration, bool (*isActiveFunc)(),
                  const char *threadName) {
  timeBeginPeriod(1);
  PrecisionTimer timer;

  while (!stopToken.stop_requested()) {
    DWORD timeout = INFINITE;

    if (configEnable.load(std::memory_order_relaxed)) {
      if (isActiveFunc()) {
        timeout = ApplyJitter(configDelay.load(std::memory_order_relaxed));
      }
    }

    if (timeout == INFINITE) {
      std::unique_lock<std::mutex> cvLock(cvMtx);
      cv.wait(cvLock, [&cvFlag, &stopToken]() { return cvFlag || stopToken.stop_requested(); });
      cvFlag = false;
    } else {
      std::unique_lock<std::mutex> cvLock(cvMtx);
      cv.wait_for(cvLock, std::chrono::milliseconds(timeout), [&cvFlag, &stopToken]() { return cvFlag || stopToken.stop_requested(); });
      cvFlag = false;
    }

    if (stopToken.stop_requested())
      break;

    if (!configEnable.load(std::memory_order_relaxed))
      continue;

    if (!isActiveFunc())
      continue;

    const int key = configKey.load(std::memory_order_relaxed);
    if (!InjectKey(key, true)) {
      continue;
    }

    DWORD duration =
        ApplyJitter(configDuration.load(std::memory_order_relaxed));
    if (duration > 0 && !timer.PreciseSleep(duration, stopToken)) {
      (void)InjectKey(key, false);
      break;
    }

    (void)InjectKey(key, false);
  }

  timeEndPeriod(1);
  Logger::GetInstance().Log(std::string(threadName) + " thread exiting.");
}

// -----------------------------------------------------------------------
// Generic stop helper — avoids copy-paste across all worker threads.
// -----------------------------------------------------------------------
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

void TurboLootThreadFunc(std::stop_token stopToken) {
  RunTurboLoop(stopToken, g_turboLootCvMtx, g_turboLootCv, g_turboLootCvFlag,
               Config::EnableTurboLoot, Config::TurboLootKey,
               Config::TurboLootDelayMs, Config::TurboLootDurationMs,
               KeybindManager::IsTurboLootActive, "TurboLoot");
}

void TurboJumpThreadFunc(std::stop_token stopToken) {
  RunTurboLoop(stopToken, g_turboJumpCvMtx, g_turboJumpCv, g_turboJumpCvFlag,
               Config::EnableTurboJump, Config::TurboJumpKey,
               Config::TurboJumpDelayMs, Config::TurboJumpDurationMs,
               KeybindManager::IsTurboJumpActive, "TurboJump");
}

} // namespace

// ---- Public API ----

bool StartTurboLootThread() {
  StopTurboLootThread();
  g_turboLootThread = std::jthread(TurboLootThreadFunc);
  Logger::GetInstance().Log("TurboLoot thread started.");
  return true;
}

void StopTurboLootThread() {
  StopWorkerThread(g_turboLootThread, TriggerTurboLoot, "TurboLoot");
}

bool StartTurboJumpThread() {
  StopTurboJumpThread();
  g_turboJumpThread = std::jthread(TurboJumpThreadFunc);
  Logger::GetInstance().Log("TurboJump thread started.");
  return true;
}

void StopTurboJumpThread() {
  StopWorkerThread(g_turboJumpThread, TriggerTurboJump, "TurboJump");
}

void TriggerTurboLoot() noexcept {
  {
    std::lock_guard<std::mutex> lock(g_turboLootCvMtx);
    g_turboLootCvFlag = true;
  }
  g_turboLootCv.notify_all();
}

void TriggerTurboJump() noexcept {
  {
    std::lock_guard<std::mutex> lock(g_turboJumpCvMtx);
    g_turboJumpCvFlag = true;
  }
  g_turboJumpCv.notify_all();
}
