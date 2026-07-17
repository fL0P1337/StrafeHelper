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

bool HandleFeatureKeyEvent(int vkCode, bool isKeyDown) noexcept {
  // ------------------------------------------------------------
  // Strict edge tracking: drop autorepeats.
  //
  // - Autorepeat: Windows fires a continuous stream of WM_KEYDOWN for held
  //   keys with no intervening WM_KEYUP. Every event drove the full feature
  //   chain (axis mutex, publish, cv notify) for zero state change. We now
  //   short-circuit any event that doesn't actually change the cached
  //   physical state of the VK.
  //
  // Opposite edges are always authoritative. Dropping one would let the
  // target application observe a transition that our cached state missed.
  // ------------------------------------------------------------
  if (vkCode >= 0 && vkCode < 256) {
    auto &state = Globals::g_KeyInfo[vkCode];
    const bool prevDown = state.physicalKeyDown.load(std::memory_order_acquire);
    if (prevDown == isKeyDown) {
      // No state change — autorepeat or echo. Don't forward to features and
      // don't suppress at OS level (the game handles autorepeat itself).
      return false;
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
