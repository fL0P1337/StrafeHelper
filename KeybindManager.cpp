// KeybindManager.cpp
// Centralized keybind mode handling with edge detection for toggle support.
#include "KeybindManager.h"
#include "Config.h"
#include "Globals.h"
#include "Logger.h"
#include <array>
#include <atomic>
#include <string>
#include <windows.h>

namespace KeybindManager {

// Per-VK previous-state tracking for edge detection. The former
// std::map<int,bool> + std::mutex required heap allocation, a hash/tree
// lookup, and a global lock on every keystroke. The fixed-size array of
// atomics indexed by VK eliminates allocation, lock contention, and cache
// misses for the same observable behaviour. Single dispatch thread today,
// but atomics keep us safe if that changes.
static std::array<std::atomic<bool>, 256> g_previousKeyState{};
static std::atomic<int> g_pendingBindReleaseVk{0};
static std::atomic<BindResult> g_lastBindResult{BindResult::None};

void Initialize() {
  g_lastBindResult.store(BindResult::None, std::memory_order_relaxed);
  for (auto &v : g_previousKeyState) {
    v.store(false, std::memory_order_relaxed);
  }
}

KeyAction ProcessKeyEvent(int vkCode, bool isKeyDown, int configKey,
                          std::atomic<Config::KeybindMode> &mode,
                          std::atomic<bool> &toggleActive) {
  KeyAction action;
  action.isKeyDown = isKeyDown;

  // Only process if this is the configured key
  if (vkCode != configKey) {
    return action;
  }

  if (vkCode < 0 || vkCode >= 256) {
    return action;
  }

  const bool wasKeyDown = g_previousKeyState[static_cast<size_t>(vkCode)]
                              .exchange(isKeyDown, std::memory_order_relaxed);

  // Detect key-down edge (up -> down transition)
  bool keyDownEdge = isKeyDown && !wasKeyDown;
  action.keyDownEdge = keyDownEdge;

  const Config::KeybindMode currentMode = mode.load(std::memory_order_relaxed);

  if (currentMode == Config::KeybindMode::Hold) {
    // Hold mode: active while key is pressed
    action.shouldActivate = isKeyDown;
    action.shouldDeactivate = !isKeyDown;
  } else {
    // Toggle mode: invert state on key-down edge only
    if (keyDownEdge) {
      bool currentActive = toggleActive.load(std::memory_order_relaxed);
      bool newActive = !currentActive;
      toggleActive.store(newActive, std::memory_order_relaxed);
      action.shouldActivate = newActive;
      action.shouldDeactivate = !newActive;
    }
    // In toggle mode, key-up does nothing
  }

  return action;
}

// -----------------------------------------------------------------------
// Generic helpers — eliminate per-feature copy-paste
// -----------------------------------------------------------------------

// Returns whether a feature with the given config key+mode is currently active
// based on physical key hold state or toggle state.
static bool IsFeatureActive(std::atomic<int> &configKey,
                            std::atomic<Config::KeybindMode> &mode,
                            std::atomic<bool> &toggleActive) {
  if (mode.load(std::memory_order_relaxed) == Config::KeybindMode::Hold) {
    const int vk = configKey.load(std::memory_order_relaxed);
    if (vk >= 0 && vk < 256)
      return Globals::g_KeyInfo[vk].physicalKeyDown.load(std::memory_order_relaxed);
    return false;
  }
  return toggleActive.load(std::memory_order_relaxed);
}

// Processes a key event for a feature and returns whether the feature should
// be active after this event.
static bool ProcessFeatureKeyEvent(int vkCode, bool isKeyDown,
                                   std::atomic<int> &configKey,
                                   std::atomic<Config::KeybindMode> &mode,
                                   std::atomic<bool> &toggleActive) {
  const int key = configKey.load(std::memory_order_relaxed);
  if (vkCode != key) {
    return IsFeatureActive(configKey, mode, toggleActive);
  }

  auto action = ProcessKeyEvent(vkCode, isKeyDown, key, mode, toggleActive);

  if (mode.load(std::memory_order_relaxed) == Config::KeybindMode::Hold) {
    return action.isKeyDown;
  }
  return toggleActive.load(std::memory_order_relaxed);
}

// -----------------------------------------------------------------------
// Feature-specific processors (thin wrappers over the generic helpers)
// -----------------------------------------------------------------------

bool ProcessSpamTrigger(int vkCode, bool isKeyDown) {
  return ProcessFeatureKeyEvent(vkCode, isKeyDown,
                                Config::KeySpamTrigger,
                                Config::KeySpamTriggerMode,
                                Config::KeySpamTriggerToggleActive);
}

bool ProcessTurboLoot(int vkCode, bool isKeyDown) {
  return ProcessFeatureKeyEvent(vkCode, isKeyDown,
                                Config::TurboLootKey,
                                Config::TurboLootMode,
                                Config::TurboLootToggleActive);
}

bool ProcessTurboJump(int vkCode, bool isKeyDown) {
  return ProcessFeatureKeyEvent(vkCode, isKeyDown,
                                Config::TurboJumpKey,
                                Config::TurboJumpMode,
                                Config::TurboJumpToggleActive);
}

bool ProcessSuperglide(int vkCode, bool isKeyDown) {
  const int bindKey = Config::SuperglideBind.load(std::memory_order_relaxed);
  if (vkCode != bindKey) {
    return false;
  }

  // Superglide is always hold-mode internally (one-shot on key-down edge).
  Config::SuperglideMode.store(Config::KeybindMode::Hold,
                               std::memory_order_relaxed);
  Config::SuperglideToggleActive.store(false, std::memory_order_relaxed);

  auto action =
      ProcessKeyEvent(vkCode, isKeyDown, bindKey, Config::SuperglideMode,
                      Config::SuperglideToggleActive);
  Config::SuperglideToggleActive.store(false, std::memory_order_relaxed);
  return action.keyDownEdge;
}

// -----------------------------------------------------------------------
// Active-state queries
// -----------------------------------------------------------------------

bool IsSpamTriggerActive() {
  return IsFeatureActive(Config::KeySpamTrigger,
                         Config::KeySpamTriggerMode,
                         Config::KeySpamTriggerToggleActive);
}

bool IsTurboLootActive() {
  return IsFeatureActive(Config::TurboLootKey,
                         Config::TurboLootMode,
                         Config::TurboLootToggleActive);
}

bool IsTurboJumpActive() {
  return IsFeatureActive(Config::TurboJumpKey,
                         Config::TurboJumpMode,
                         Config::TurboJumpToggleActive);
}

bool IsSuperglideActive() {
  return false; // One-shot trigger, no persistent active state.
}

BindResult GetLastBindResult() noexcept {
  return g_lastBindResult.load(std::memory_order_acquire);
}

void ClearLastBindResult() noexcept {
  g_lastBindResult.store(BindResult::None, std::memory_order_release);
}

// -----------------------------------------------------------------------
// Dynamic keybinding capture
// -----------------------------------------------------------------------

bool HandleBind(int vkCode, bool isKeyDown) {
  const int pendingRelease =
      g_pendingBindReleaseVk.load(std::memory_order_acquire);
  if (!isKeyDown && pendingRelease == vkCode) {
    Globals::g_KeyInfo[vkCode].physicalKeyDown.store(
        false, std::memory_order_release);
    g_previousKeyState[static_cast<size_t>(vkCode)].store(
        false, std::memory_order_relaxed);
    g_pendingBindReleaseVk.store(0, std::memory_order_release);
    return true;
  }

  std::atomic<int> *target = Globals::g_bindingTarget.load();
  if (!target) {
    return false;
  }

  if (!isKeyDown) {
    return true;
  }

  if (vkCode == VK_ESCAPE) {
    g_lastBindResult.store(BindResult::Cancelled, std::memory_order_release);
    Globals::g_bindingTarget.store(nullptr);
    return true;
  }

  if (vkCode == VK_LBUTTON || vkCode == VK_RBUTTON || vkCode == VK_MBUTTON ||
      vkCode == VK_LWIN || vkCode == VK_RWIN || vkCode <= 0 || vkCode >= 256) {
    g_lastBindResult.store(BindResult::Unsupported, std::memory_order_release);
    Globals::g_bindingTarget.store(nullptr);
    return true;
  }

  const std::atomic<int> *bindings[] = {
      &Config::KeySpamTrigger,
      &Config::TurboLootKey,
      &Config::TurboJumpKey,
      &Config::SuperglideBind,
  };
  for (const auto *binding : bindings) {
    if (binding != target &&
        binding->load(std::memory_order_relaxed) == vkCode) {
      g_lastBindResult.store(BindResult::Duplicate, std::memory_order_release);
      return true;
    }
  }

  target->store(vkCode, std::memory_order_relaxed);
  g_lastBindResult.store(BindResult::Success, std::memory_order_release);
  Globals::g_KeyInfo[vkCode].physicalKeyDown.store(true,
                                                    std::memory_order_release);
  g_previousKeyState[static_cast<size_t>(vkCode)].store(
      true, std::memory_order_relaxed);
  g_pendingBindReleaseVk.store(vkCode, std::memory_order_release);
  Globals::g_bindingTarget.store(nullptr);

  if (Globals::g_hWindow) {
    PostMessageW(Globals::g_hWindow, Globals::WM_DEFERRED_CONFIG_SAVE, 0, 0);
  }
  return true;
}

} // namespace KeybindManager
