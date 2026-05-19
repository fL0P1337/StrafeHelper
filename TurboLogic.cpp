// TurboLogic.cpp
#include "TurboLogic.h"
#include "Config.h"
#include "Globals.h"
#include "KeybindManager.h"
#include "Utils.h"
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <random>
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

void RunTurboLoop(std::stop_token stopToken,
                  std::mutex &cvMtx, std::condition_variable &cv, bool &cvFlag,
                  std::atomic<bool> &configEnable, std::atomic<int> &configKey,
                  std::atomic<int> &configDelay,
                  std::atomic<int> &configDuration, bool (*isActiveFunc)(),
                  const char *threadName) {
  timeBeginPeriod(1);

  LARGE_INTEGER freq;
  QueryPerformanceFrequency(&freq);

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

    INPUT input = {INPUT_KEYBOARD};
    input.ki.wVk = 0;
    input.ki.wScan = VirtualKeyToScanCode(key);
    input.ki.dwFlags = VirtualKeyInputFlags(key, true);
    input.ki.dwExtraInfo = GetMessageExtraInfo();
    SendInput(1, &input, sizeof(INPUT));

    DWORD duration =
        ApplyJitter(configDuration.load(std::memory_order_relaxed));
    if (duration > 0) {
      LARGE_INTEGER start;
      QueryPerformanceCounter(&start);
      // duration is in milliseconds. Convert to ticks: duration * (freq / 1000)
      const LONGLONG durationTicks =
          static_cast<LONGLONG>(duration) * (freq.QuadPart / 1000LL);
      const LONGLONG targetTick = start.QuadPart + durationTicks;
      const LONGLONG spinThreshold = freq.QuadPart / 2000LL; // 0.5 ms in ticks

      LARGE_INTEGER now;
      do {
        QueryPerformanceCounter(&now);
        const LONGLONG remaining = targetTick - now.QuadPart;

        if (stopToken.stop_requested()) {
          break;
        }

        if (remaining > spinThreshold) {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
      } while (now.QuadPart < targetTick);

      if (stopToken.stop_requested())
        break;
    }

    // Send key-up
    input.ki.dwFlags = VirtualKeyInputFlags(key, false);
    SendInput(1, &input, sizeof(INPUT));
  }

  timeEndPeriod(1);
  std::cout << threadName << " thread exiting." << std::endl;
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
  std::cout << "TurboLoot thread started." << std::endl;
  return true;
}

void StopTurboLootThread() {
  if (g_turboLootThread.joinable()) {
    std::cout << "Requesting TurboLoot thread stop..." << std::endl;
    g_turboLootThread.request_stop();
    TriggerTurboLoot();
    g_turboLootThread.join();
    std::cout << "TurboLoot thread stopped." << std::endl;
  }
}

bool StartTurboJumpThread() {
  StopTurboJumpThread();
  g_turboJumpThread = std::jthread(TurboJumpThreadFunc);
  std::cout << "TurboJump thread started." << std::endl;
  return true;
}

void StopTurboJumpThread() {
  if (g_turboJumpThread.joinable()) {
    std::cout << "Requesting TurboJump thread stop..." << std::endl;
    g_turboJumpThread.request_stop();
    TriggerTurboJump();
    g_turboJumpThread.join();
    std::cout << "TurboJump thread stopped." << std::endl;
  }
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
