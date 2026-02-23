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
#include "Config.h"
#include "Globals.h"
#include "Utils.h"
#include <atomic>
#include <iostream>
#include <timeapi.h> // timeBeginPeriod / timeEndPeriod
#include <windows.h>

#pragma comment(lib, "winmm.lib")

namespace {

// VK codes for the injected sequence (Apex Legends defaults)
constexpr int kJumpVK = VK_SPACE;      // Jump
constexpr int kCrouchVK = VK_LCONTROL; // Crouch (left Ctrl)

std::atomic<bool> g_stopSuperglideRequest{false};

// Injects a single key tap (down then up) using KEYEVENTF_SCANCODE.
// Matches the injection style used throughout the rest of the codebase.
static void InjectKeyTap(int vk) noexcept {
  const WORD sc = static_cast<WORD>(MapVirtualKey(vk, MAPVK_VK_TO_VSC));
  INPUT inputs[2]{};

  inputs[0].type = INPUT_KEYBOARD;
  inputs[0].ki.wVk = 0;
  inputs[0].ki.wScan = sc;
  inputs[0].ki.dwFlags = KEYEVENTF_SCANCODE;

  inputs[1].type = INPUT_KEYBOARD;
  inputs[1].ki.wVk = 0;
  inputs[1].ki.wScan = sc;
  inputs[1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;

  SendInput(2, inputs, sizeof(INPUT));
}

DWORD WINAPI SuperglideThread(LPVOID) {
  // Raise timer resolution to ~1 ms so Sleep is accurate enough for
  // sub-frame timing at high frame rates.
  timeBeginPeriod(1);

  // Cache QPC frequency — constant for the lifetime of the process.
  LARGE_INTEGER freq;
  QueryPerformanceFrequency(&freq);

  while (!g_stopSuperglideRequest.load(std::memory_order_relaxed)) {
    WaitForSingleObject(Globals::g_hSuperglideEvent, INFINITE);

    if (g_stopSuperglideRequest.load(std::memory_order_relaxed))
      break;

    if (!Config::EnableSuperglide.load(std::memory_order_relaxed))
      continue;

    const double targetFPS = Config::TargetFPS.load(std::memory_order_relaxed);

    // Exact frame duration in QPC ticks — no integer truncation.
    // (DWORD-cast was losing the fractional ms, causing consistent undershoot)
    const LONGLONG frameTimeTicks =
        static_cast<LONGLONG>(static_cast<double>(freq.QuadPart) / targetFPS);

    // Spin threshold: stop coarse-sleeping 0.5 ms before the target so we
    // don't overshoot, then busy-spin for the remainder.
    const LONGLONG spinThreshold = freq.QuadPart / 2000LL; // 0.5 ms in ticks

    // 1. Jump tap — record timing immediately after injection so the
    //    measurement baseline matches the practice tool.
    InjectKeyTap(kJumpVK);

    LARGE_INTEGER start;
    QueryPerformanceCounter(&start);
    const LONGLONG targetTick = start.QuadPart + frameTimeTicks;

    // 2. Hybrid wait: coarse Sleep(1) releases the CPU for most of the frame,
    //    then we spin on QPC for the final ~0.5 ms for precision.
    LARGE_INTEGER now;
    do {
      QueryPerformanceCounter(&now);
      const LONGLONG remaining = targetTick - now.QuadPart;
      if (remaining > spinThreshold) {
        Sleep(1);
      }
    } while (now.QuadPart < targetTick);

    if (g_stopSuperglideRequest.load(std::memory_order_relaxed))
      break;

    // 3. Crouch tap — fires at exactly 1 frame after Jump.
    InjectKeyTap(kCrouchVK);

    // Cooldown: ignore re-triggers for 500 ms after each execution.
    // Prevents key-hold auto-repeat or rapid presses from spamming the
    // sequence.
    Sleep(500);
  }

  timeEndPeriod(1);
  std::cout << "Superglide thread exiting." << std::endl;
  return 0;
}

} // namespace

// ---- Public API ----

bool StartSuperglideThread() {
  g_stopSuperglideRequest.store(false);
  Globals::g_hSuperglideThread =
      CreateThread(NULL, 0, SuperglideThread, NULL, 0, NULL);
  if (!Globals::g_hSuperglideThread) {
    LogError("CreateThread for SuperglideThread failed");
    return false;
  }
  std::cout << "Superglide thread started." << std::endl;
  return true;
}

void StopSuperglideThread() {
  if (Globals::g_hSuperglideThread) {
    std::cout << "Requesting Superglide thread stop..." << std::endl;
    g_stopSuperglideRequest.store(true);
    if (Globals::g_hSuperglideEvent)
      SetEvent(Globals::g_hSuperglideEvent);
    WaitForSingleObject(Globals::g_hSuperglideThread, 1000);
    CloseHandle(Globals::g_hSuperglideThread);
    Globals::g_hSuperglideThread = NULL;
    std::cout << "Superglide thread stopped." << std::endl;
  }
  g_stopSuperglideRequest.store(false);
}
