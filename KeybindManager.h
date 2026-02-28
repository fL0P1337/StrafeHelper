// KeybindManager.h
// Centralized keybind mode handling with edge detection for toggle support.
#pragma once

#include "Config.h"
#include <atomic>
#include <map>

namespace KeybindManager {

// Result of processing a key event
struct KeyAction {
  bool shouldActivate = false;   // Feature should become active
  bool shouldDeactivate = false; // Feature should become inactive
  bool isKeyDown = false;        // Physical key state
  bool keyDownEdge = false;      // True only on up -> down transition
};

// Initialize the manager (called once at startup)
void Initialize();

// Process a key event and return the action for a feature.
// This handles both Hold and Toggle modes with edge detection.
KeyAction ProcessKeyEvent(int vkCode, bool isKeyDown, int configKey,
                          std::atomic<Config::KeybindMode> &mode,
                          std::atomic<bool> &toggleActive);

// Feature-specific processors
// Returns true if the feature should be active after this event.

// Spam trigger: returns true if spam should be active
bool ProcessSpamTrigger(int vkCode, bool isKeyDown);

// Turbo Loot: returns true if turbo loot should be active
bool ProcessTurboLoot(int vkCode, bool isKeyDown);

// Turbo Jump: returns true if turbo jump should be active
bool ProcessTurboJump(int vkCode, bool isKeyDown);

// Superglide: returns true if superglide should trigger
bool ProcessSuperglide(int vkCode, bool isKeyDown);

// Get current active state for each feature (considering both mode and toggle state)
bool IsSpamTriggerActive();
bool IsTurboLootActive();
bool IsTurboJumpActive();
bool IsSuperglideActive();

// Handle dynamic keybinding (returns true if event was consumed/suppressed)
bool HandleBind(int vkCode, bool isKeyDown);

} // namespace KeybindManager
