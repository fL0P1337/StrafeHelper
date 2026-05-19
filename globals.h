// globals.h
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <atomic>
#include <mutex>
#include <vector>
#include <windows.h>
#include <shellapi.h>

namespace Globals {

// --- WinAPI Handles ---
extern HWND g_hWindow;
extern HINSTANCE g_hInstance;

// --- State Management ---
struct KeyState {
  std::atomic<bool> physicalKeyDown{false};
  std::atomic<bool> spamming{false};

  KeyState() : physicalKeyDown(false), spamming(false) {}
  KeyState(const KeyState &other)
      : physicalKeyDown(other.physicalKeyDown.load()),
        spamming(other.spamming.load()) {}
  KeyState &operator=(const KeyState &other) {
    if (this != &other) {
      physicalKeyDown.store(other.physicalKeyDown.load());
      spamming.store(other.spamming.load());
    }
    return *this;
  }
  KeyState(KeyState &&other) noexcept
      : physicalKeyDown(other.physicalKeyDown.load()),
        spamming(other.spamming.load()) {}
  KeyState &operator=(KeyState &&other) noexcept {
    if (this != &other) {
      physicalKeyDown.store(other.physicalKeyDown.load());
      spamming.store(other.spamming.load());
    }
    return *this;
  }
};

// Fixed-size array indexed by VK code (0x00–0xFF).
// Direct array access replaces the former std::map — no allocation,
// no iterator invalidation, and no map-structure race.
extern KeyState g_KeyInfo[256];

extern std::vector<int> g_activeSpamKeys;
extern std::atomic<unsigned long long> g_spamKeysEpoch;
extern std::mutex g_activeKeysMutex;
extern std::atomic<bool> g_isSpamActive; // Renamed from g_isCSpamActive

// --- Superglide Stats ---
struct SuperglideResult {
  double elapsedFrames;
  double chancePercent;
  double errorMs;
};

constexpr int kSuperglideHistorySize = 32;

struct SuperglideStats {
  SuperglideResult history[kSuperglideHistorySize]{};
  std::atomic<int> count{0};
  std::atomic<int> writeIdx{0};
};

extern SuperglideStats g_superglideStats;

// --- Binding State ---
extern std::atomic<std::atomic<int>*> g_bindingTarget;

// --- Tray Icon ---
extern NOTIFYICONDATA g_nid;

// Tray icon constants (constexpr replaces #define; macros ignore namespaces)
inline constexpr UINT WM_TRAYICON           = WM_APP + 1;
inline constexpr UINT ID_TRAY_APP_ICON      = 1001;
inline constexpr UINT ID_TRAY_EXIT_MENU_ITEM    = 3000;
inline constexpr UINT ID_TRAY_TOGGLE_SPAM_ITEM  = 3002;
inline constexpr UINT ID_TRAY_TOGGLE_SNAPTAP_ITEM = 3003;

} // namespace Globals
