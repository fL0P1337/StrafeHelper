// EventDispatcher.cpp
//
// Implements HandleFeatureKeyEvent — the core feature routing logic.
//
// Dependency map:
//   Config      — feature enable flags, key bindings, timing parameters
//   Globals     — g_KeyInfo (physical key state), g_isSpamActive
//   KeybindManager — hold/toggle mode processing per feature
//   MovementStateManager — WASD / SnapTap / Spam state transitions
//   SpamLogic   — OnSpamActivated / OnSpamDeactivated (delegated to MSM)
//   TurboLogic  — wakeup triggers for TurboLoot / TurboJump threads
//   SuperglideLogic — wakeup trigger for the Superglide thread
//   Logger      — structured log output
//
// Notably absent: InputBackend, InterceptionBackend, KbdHookBackend.
// This file knows nothing about how events arrive — only how to route them.

#include "EventDispatcher.h"
#include "Config.h"
#include "Globals.h"
#include "KeybindManager.h"
#include "MovementStateManager.h"
#include "SpamLogic.h"
#include "SuperglideLogic.h"
#include "TurboLogic.h"

#include <chrono>

namespace {

// Returns the current steady-clock timestamp in nanoseconds. steady_clock
// is monotonically increasing and is backed by QPC on Windows, so this is
// the same time source the spam thread observes via PrecisionTimer.
[[nodiscard]] inline int64_t NowNs() noexcept {
  return std::chrono::steady_clock::now().time_since_epoch().count();
}

} // namespace

bool HandleFeatureKeyEvent(int vkCode, bool isKeyDown) noexcept {
  // ------------------------------------------------------------
  // Strict edge tracking: drop autorepeats and bounces.
  //
  // - Autorepeat: Windows fires a continuous stream of WM_KEYDOWN for held
  //   keys with no intervening WM_KEYUP. Every event drove the full feature
  //   chain (axis mutex, publish, cv notify) for zero state change. We now
  //   short-circuit any event that doesn't actually change the cached
  //   physical state of the VK.
  //
  // - Bounce / driver echo: opposite-edge transitions that arrive within
  //   DebounceUs of the previous transition are treated as chatter and
  //   dropped. The default 500 µs window is far below human dexterity but
  //   above typical mechanical-switch debounce.
  //
  // Mouse buttons (X1/X2) live in the same VK space and benefit from the
  // same edge/debounce filter. The dispatch routing below cares only about
  // genuine transitions, so suppressing pseudo-events here is safe.
  // ------------------------------------------------------------
  if (vkCode >= 0 && vkCode < 256) {
    auto &state = Globals::g_KeyInfo[vkCode];
    const bool prevDown = state.physicalKeyDown.load(std::memory_order_acquire);
    if (prevDown == isKeyDown) {
      // No state change — autorepeat or echo. Don't forward to features and
      // don't suppress at OS level (the game handles autorepeat itself).
      return false;
    }

    const int debounceUs = Config::DebounceUs.load(std::memory_order_relaxed);
    if (debounceUs > 0) {
      const int64_t nowNs = NowNs();
      const int64_t lastNs = state.lastEdgeNs.load(std::memory_order_relaxed);
      const int64_t debounceNs = static_cast<int64_t>(debounceUs) * 1000;
      // First edge for this VK (lastNs == 0) is never treated as bounce.
      if (lastNs != 0 && (nowNs - lastNs) < debounceNs) {
        return false; // Bounce — drop silently.
      }
      state.lastEdgeNs.store(nowNs, std::memory_order_relaxed);
    } else {
      state.lastEdgeNs.store(NowNs(), std::memory_order_relaxed);
    }

    // Publish the new physical state with release semantics so observers
    // on the spam / GUI thread that load with acquire see a consistent view.
    state.physicalKeyDown.store(isKeyDown, std::memory_order_release);
  }

  const bool spamFeatureEnabled =
      Config::EnableSpam.load(std::memory_order_relaxed);
  const bool snapTapEnabled =
      Config::EnableSnapTap.load(std::memory_order_relaxed);
  const int currentTriggerKey =
      Config::KeySpamTrigger.load(std::memory_order_relaxed);

  // --- Spam trigger — gates movement key spamming ---
  if (vkCode == currentTriggerKey && spamFeatureEnabled) {
    const bool shouldBeActive =
        KeybindManager::ProcessSpamTrigger(vkCode, isKeyDown);

    if (shouldBeActive &&
        !Globals::g_isSpamActive.load(std::memory_order_acquire)) {
      Globals::g_isSpamActive.store(true, std::memory_order_release);
      OnSpamActivated(snapTapEnabled);
    } else if (!shouldBeActive &&
               Globals::g_isSpamActive.load(std::memory_order_acquire)) {
      OnSpamDeactivated(snapTapEnabled);
    }
    // Spam trigger key itself is never forwarded to the game.
    return true;
  }

  // --- Turbo Loot ---
  if (vkCode == Config::TurboLootKey.load(std::memory_order_relaxed) &&
      Config::EnableTurboLoot.load(std::memory_order_relaxed)) {
    KeybindManager::ProcessTurboLoot(vkCode, isKeyDown);
    TriggerTurboLoot();
    return false;
  }

  // --- Turbo Jump ---
  if (vkCode == Config::TurboJumpKey.load(std::memory_order_relaxed) &&
      Config::EnableTurboJump.load(std::memory_order_relaxed)) {
    KeybindManager::ProcessTurboJump(vkCode, isKeyDown);
    TriggerTurboJump();
    return false;
  }

  // --- Superglide — suppress so the raw key never reaches the game ---
  if (vkCode == Config::SuperglideBind.load(std::memory_order_relaxed) &&
      Config::EnableSuperglide.load(std::memory_order_relaxed)) {
    if (KeybindManager::ProcessSuperglide(vkCode, isKeyDown)) {
      TriggerSuperglide();
    }
    return true; // Always suppressed; the macro injects its own sequence.
  }

  // --- WASD / SnapTap movement state ---
  const bool spamActive =
      Globals::g_isSpamActive.load(std::memory_order_acquire) &&
      spamFeatureEnabled;
  if (HandleMovementKeyState(vkCode, isKeyDown, spamActive, snapTapEnabled)) {
    return true;
  }

  return false;
}
