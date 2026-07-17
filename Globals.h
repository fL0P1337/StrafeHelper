// globals.h
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <atomic>
#include <cstdint>
#include <mutex>
#include <windows.h>

namespace Globals {

// --- WinAPI Handles ---
extern HWND g_hWindow;
extern HINSTANCE g_hInstance;

// --- State Management ---
struct KeyState {
  std::atomic<bool> physicalKeyDown{false};
  std::atomic<bool> spamming{false};

  KeyState() = default;
  KeyState(const KeyState &) = delete;
  KeyState &operator=(const KeyState &) = delete;
  KeyState(KeyState &&) = delete;
  KeyState &operator=(KeyState &&) = delete;
};

// Fixed-size array indexed by VK code (0x00–0xFF).
// Direct array access replaces the former std::map — no allocation,
// no iterator invalidation, and no map-structure race.
extern KeyState g_KeyInfo[256];

// --- Lurch (spam) shared state ---
// `g_lurchState` packs the per-key spam bitmask (low 4 bits) and a 24-bit
// epoch counter (high 24 bits) into a single 32-bit atomic. This replaces
// the former `g_activeSpamKeys` vector + `g_activeKeysMutex` pair so the
// hook-thread and spam-thread no longer take a mutex on the hot path.
//
// Layout:
//   bit 0 = 'W' spamming
//   bit 1 = 'A' spamming
//   bit 2 = 'S' spamming
//   bit 3 = 'D' spamming
//   bits 4-7 reserved
//   bits 8-31 = epoch (monotonic, wraps every 16M edges — fine for race
//               detection over the lifetime of a spam burst)
inline constexpr uint32_t kLurchBitW = 1u << 0;
inline constexpr uint32_t kLurchBitA = 1u << 1;
inline constexpr uint32_t kLurchBitS = 1u << 2;
inline constexpr uint32_t kLurchBitD = 1u << 3;
inline constexpr uint32_t kLurchKeyMask = 0x0Fu;
inline constexpr uint32_t kLurchEpochShift = 8u;
inline constexpr uint32_t kLurchEpochInc = 1u << kLurchEpochShift;

extern std::atomic<uint32_t> g_lurchState;
extern std::atomic<bool> g_isSpamActive; // Renamed from g_isCSpamActive

[[nodiscard]] inline constexpr uint32_t LurchBitForVk(int vk) noexcept {
  switch (vk) {
  case 'W':
    return kLurchBitW;
  case 'A':
    return kLurchBitA;
  case 'S':
    return kLurchBitS;
  case 'D':
    return kLurchBitD;
  default:
    return 0u;
  }
}

// Decodes the active-key mask into a fixed-size array of VK codes.
// Returns the number of keys written (0..4). The caller-supplied array
// must have room for at least 4 entries.
[[nodiscard]] inline size_t DecodeLurchKeys(uint32_t mask, int (&out)[4]) noexcept {
  size_t n = 0;
  if (mask & kLurchBitW) out[n++] = 'W';
  if (mask & kLurchBitA) out[n++] = 'A';
  if (mask & kLurchBitS) out[n++] = 'S';
  if (mask & kLurchBitD) out[n++] = 'D';
  return n;
}

// --- Superglide Stats ---
struct SuperglideResult {
  double elapsedFrames;
  double chancePercent;
  double errorMs;
};

constexpr int kSuperglideHistorySize = 32;

struct SuperglideStats {
  std::mutex mutex;
  SuperglideResult history[kSuperglideHistorySize]{};
  std::atomic<int> count{0};
  std::atomic<int> writeIdx{0};
};

extern SuperglideStats g_superglideStats;

// --- Binding State ---
extern std::atomic<std::atomic<int>*> g_bindingTarget;

// --- Application messages ---
inline constexpr UINT WM_DEFERRED_CONFIG_SAVE = WM_APP + 1;
inline constexpr UINT WM_BACKEND_FAILED = WM_APP + 2;
inline constexpr UINT WM_INJECTION_FAILED = WM_APP + 3;

} // namespace Globals
