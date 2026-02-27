// KeybindManager.cpp
#include "KeybindManager.h"
#include "Config.h"
#include <map>
#include <mutex>
#include <windows.h>
#include "Globals.h"

namespace KeybindManager {

// Track previous key state for edge detection
// Key: VK code, Value: was key down on previous event
static std::map<int, bool> g_previousKeyState;
static std::mutex g_stateMutex;

void Initialize() {
  std::lock_guard<std::mutex> lock(g_stateMutex);
  g_previousKeyState.clear();
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

  std::lock_guard<std::mutex> lock(g_stateMutex);

  // Get previous state (default to false if not tracked)
  bool wasKeyDown = false;
  auto it = g_previousKeyState.find(vkCode);
  if (it != g_previousKeyState.end()) {
    wasKeyDown = it->second;
  }

  // Update tracked state
  g_previousKeyState[vkCode] = isKeyDown;

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

bool ProcessSpamTrigger(int vkCode, bool isKeyDown) {
  const int triggerKey = Config::KeySpamTrigger.load(std::memory_order_relaxed);
  if (vkCode != triggerKey) {
    // Return current state if not the trigger key
    return IsSpamTriggerActive();
  }

  auto action = ProcessKeyEvent(vkCode, isKeyDown, triggerKey,
                                Config::KeySpamTriggerMode,
                                Config::KeySpamTriggerToggleActive);

  if (Config::KeySpamTriggerMode.load() == Config::KeybindMode::Hold) {
    return action.isKeyDown;
  } else {
    return Config::KeySpamTriggerToggleActive.load();
  }
}

bool ProcessTurboLoot(int vkCode, bool isKeyDown) {
  const int lootKey = Config::TurboLootKey.load(std::memory_order_relaxed);
  if (vkCode != lootKey) {
    return IsTurboLootActive();
  }

  auto action = ProcessKeyEvent(vkCode, isKeyDown, lootKey, Config::TurboLootMode,
                                Config::TurboLootToggleActive);

  if (Config::TurboLootMode.load() == Config::KeybindMode::Hold) {
    return action.isKeyDown;
  } else {
    return Config::TurboLootToggleActive.load();
  }
}

bool ProcessTurboJump(int vkCode, bool isKeyDown) {
  const int jumpKey = Config::TurboJumpKey.load(std::memory_order_relaxed);
  if (vkCode != jumpKey) {
    return IsTurboJumpActive();
  }

  auto action = ProcessKeyEvent(vkCode, isKeyDown, jumpKey, Config::TurboJumpMode,
                                Config::TurboJumpToggleActive);

  if (Config::TurboJumpMode.load() == Config::KeybindMode::Hold) {
    return action.isKeyDown;
  } else {
    return Config::TurboJumpToggleActive.load();
  }
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

static bool PhysicalKeyHeld(int vk) {
  auto it = Globals::g_KeyInfo.find(vk);
  if (it != Globals::g_KeyInfo.end())
    return it->second.physicalKeyDown.load(std::memory_order_relaxed);
  return false;
}

bool IsSpamTriggerActive() {
  if (Config::KeySpamTriggerMode.load() == Config::KeybindMode::Hold) {
    return PhysicalKeyHeld(
        Config::KeySpamTrigger.load(std::memory_order_relaxed));
  } else {
    return Config::KeySpamTriggerToggleActive.load(std::memory_order_relaxed);
  }
}

bool IsTurboLootActive() {
  if (Config::TurboLootMode.load() == Config::KeybindMode::Hold) {
    return PhysicalKeyHeld(
        Config::TurboLootKey.load(std::memory_order_relaxed));
  } else {
    return Config::TurboLootToggleActive.load(std::memory_order_relaxed);
  }
}

bool IsTurboJumpActive() {
  if (Config::TurboJumpMode.load() == Config::KeybindMode::Hold) {
    return PhysicalKeyHeld(
        Config::TurboJumpKey.load(std::memory_order_relaxed));
  } else {
    return Config::TurboJumpToggleActive.load(std::memory_order_relaxed);
  }
}

bool IsSuperglideActive() {
  return false; // One-shot trigger, no persistent active state.
}

} // namespace KeybindManager
