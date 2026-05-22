// MovementStateManager.cpp
#include "MovementStateManager.h"
#include "Config.h"
#include "Globals.h"
#include "Application.h"
#include "KeybindManager.h"
#include "SpamLogic.h"
#include "Utils.h"
#include <algorithm>
#include <mutex>
#include <vector>
#include <windows.h>

namespace {

inline bool IsWasdVk(int vk) {
  return (vk == 'W' || vk == 'A' || vk == 'S' || vk == 'D');
}

inline bool IsAxisXKey(int vk) { return (vk == 'A' || vk == 'D'); }

inline bool IsAxisYKey(int vk) { return (vk == 'W' || vk == 'S'); }

// Per-axis SOCD / SnapTap state (last-pressed wins, revert LIFO).
// X axis: A <-> D
// Y axis: W <-> S
struct AxisState {
  int activeKey = 0;
  int heldCount = 0;
  int physicallyHeld[2] = {0, 0}; // press order (LIFO last)
};

// Mutex protecting AxisState and virtual axis state from concurrent access
// between the hook thread (HandleMovementKeyState) and the GUI thread
// (OnSnapTapToggled, RefreshMovementState).
std::mutex g_axisMutex;

AxisState g_axisX;
AxisState g_axisY;

// Virtual keys currently held down by SnapTap (only when SnapTap enabled and
// spam inactive).
int g_virtualAxisX = 0;
int g_virtualAxisY = 0;

AxisState *AxisForVk(int vk) {
  if (IsAxisXKey(vk))
    return &g_axisX;
  if (IsAxisYKey(vk))
    return &g_axisY;
  return nullptr;
}

inline bool AxisContains(const AxisState &axis, int vk) {
  return (axis.heldCount > 0 && axis.physicallyHeld[0] == vk) ||
         (axis.heldCount > 1 && axis.physicallyHeld[1] == vk);
}

// Returns true only for a real up->down transition (ignores autorepeat).
bool AxisOnKeyDown(AxisState &axis, int vk) {
  if (!AxisContains(axis, vk)) {
    if (axis.heldCount < 2) {
      axis.physicallyHeld[axis.heldCount++] = vk;
      axis.activeKey = vk;
      return true;
    }
  }
  return false;
}

bool AxisOnKeyUp(AxisState &axis, int vk) {
  if (axis.heldCount == 0) {
    return false;
  }

  if (axis.physicallyHeld[0] == vk) {
    if (axis.heldCount == 2) {
      axis.physicallyHeld[0] = axis.physicallyHeld[1];
      axis.heldCount = 1;
      axis.activeKey = axis.physicallyHeld[0];
    } else {
      axis.heldCount = 0;
      axis.activeKey = 0;
    }
    return true;
  } else if (axis.heldCount == 2 && axis.physicallyHeld[1] == vk) {
    axis.heldCount = 1;
    axis.activeKey = axis.physicallyHeld[0];
    return true;
  }
  return false;
}

void SendKeyDownImmediate(int vkCode) {
  InjectKey(vkCode, true);
}

void SendKeyUpImmediate(int vkCode) {
  InjectKey(vkCode, false);
}

// NOTE: Must be called with g_axisMutex held.
void ApplyVirtualAxisState_Locked(int &currentVk, int desiredVk) {
  if (currentVk == desiredVk)
    return;

  if (currentVk != 0) {
    SendKeyUpImmediate(currentVk);
  }
  if (desiredVk != 0) {
    SendKeyDownImmediate(desiredVk);
  }
  currentVk = desiredVk;
}

// NOTE: Must be called with g_axisMutex held.
void ReleaseSnapTapVirtualKeys_Locked() {
  ApplyVirtualAxisState_Locked(g_virtualAxisX, 0);
  ApplyVirtualAxisState_Locked(g_virtualAxisY, 0);
}

// NOTE: Must be called with g_axisMutex held.
void ApplySnapTapOutput_Locked(bool spamActive) {
  const int desiredX = spamActive ? 0 : g_axisX.activeKey;
  const int desiredY = spamActive ? 0 : g_axisY.activeKey;

  ApplyVirtualAxisState_Locked(g_virtualAxisX, desiredX);
  ApplyVirtualAxisState_Locked(g_virtualAxisY, desiredY);
}

// NOTE: Must be called with g_axisMutex held.
std::vector<int> BuildDesiredSpamKeys_Locked(bool snapTapEnabled) {
  std::vector<int> keys;
  keys.reserve(2);

  if (snapTapEnabled) {
    if (g_axisX.activeKey != 0)
      keys.push_back(g_axisX.activeKey);
    if (g_axisY.activeKey != 0)
      keys.push_back(g_axisY.activeKey);
  } else {
    for (int key : {'W', 'A', 'S', 'D'}) {
      if (Globals::g_KeyInfo[key].physicalKeyDown.load(
              std::memory_order_relaxed)) {
        keys.push_back(key);
      }
    }
  }

  return keys;
}

// NOTE: Must be called with g_axisMutex held.
void PublishSpamKeysFromState_Locked(bool snapTapEnabled) {
  const std::vector<int> desiredKeys = BuildDesiredSpamKeys_Locked(snapTapEnabled);
  bool changed = false;

  {
    std::lock_guard<std::mutex> lock(Globals::g_activeKeysMutex);
    if (Globals::g_activeSpamKeys != desiredKeys) {
      Globals::g_activeSpamKeys = desiredKeys;
      Globals::g_spamKeysEpoch.fetch_add(1ULL, std::memory_order_relaxed);
      changed = true;
    }

    for (int key : {'W', 'A', 'S', 'D'}) {
      Globals::g_KeyInfo[key].spamming.store(false, std::memory_order_relaxed);
    }
    for (int vk : desiredKeys) {
      Globals::g_KeyInfo[vk].spamming.store(true, std::memory_order_relaxed);
    }
  }

  if (changed) {
    TriggerSpamEvent();
  }
}

void SendKeyUpForPhysicallyHeldWasd() {
  for (int key : {'W', 'A', 'S', 'D'}) {
    if (Globals::g_KeyInfo[key].physicalKeyDown.load(
            std::memory_order_relaxed)) {
      InjectKey(key, false);
    }
  }
}

void SendKeyDownForPhysicallyHeldWasd() {
  for (int key : {'W', 'A', 'S', 'D'}) {
    if (Globals::g_KeyInfo[key].physicalKeyDown.load(
            std::memory_order_relaxed)) {
      InjectKey(key, true);
    }
  }
}

} // namespace

void OnSpamActivated(bool snapTapEnabled) {
  std::lock_guard<std::mutex> lock(g_axisMutex);
  // Ensure any held movement keys are virtually UP while spamming.
  ApplySnapTapOutput_Locked(true);
  SendKeyUpForPhysicallyHeldWasd();
  PublishSpamKeysFromState_Locked(snapTapEnabled);
}

void OnSpamDeactivated(bool snapTapEnabled) {
  std::lock_guard<std::mutex> lock(g_axisMutex);
  if (snapTapEnabled) {
    CleanupSpamState(false);
    ApplySnapTapOutput_Locked(false);
  } else {
    CleanupSpamState(true);
  }
}

bool HandleMovementKeyState(int vkCode, bool isKeyDown,
                            bool spamActive, bool snapTapEnabled) {
  if (!IsWasdVk(vkCode)) {
    return false;
  }

  std::lock_guard<std::mutex> lock(g_axisMutex);

  AxisState *axis = AxisForVk(vkCode);
  if (axis) {
    if (isKeyDown) {
      AxisOnKeyDown(*axis, vkCode);
    } else {
      AxisOnKeyUp(*axis, vkCode);
    }
  }

  if (spamActive) {
    // Spam layer keeps movement keys virtually UP and spams the active key(s).
    // NOTE: We intentionally do NOT inject a key-up here.  The physical event
    // is already suppressed (return true), and OnSpamActivated() already sent
    // key-up for any keys that were held when spam started.  Injecting an
    // extra key-up on every event (including autorepeat) races with the spam
    // thread's own down/up cycle and can leave keys stuck in the target app.
    ApplySnapTapOutput_Locked(true);
    PublishSpamKeysFromState_Locked(snapTapEnabled);
    return true;
  }

  if (snapTapEnabled) {
    // SnapTap is always-on (even without trigger). It owns WASD output when
    // enabled.
    ApplySnapTapOutput_Locked(false);
    return true;
  }

  return false;
}

void OnSnapTapToggled(bool enabled) {
  const bool spamFeatureEnabled =
      Config::EnableSpam.load(std::memory_order_relaxed);
  const bool spamActive =
      Globals::g_isSpamActive.load(std::memory_order_relaxed) &&
      spamFeatureEnabled;

  std::lock_guard<std::mutex> lock(g_axisMutex);

  if (enabled) {
    // Clear any physical holds currently down in the target app before SnapTap
    // takes over.
    if (!spamActive) {
      SendKeyUpForPhysicallyHeldWasd();
    }
    ApplySnapTapOutput_Locked(spamActive);
  } else {
    // Release any SnapTap-owned keys so we don't leave the target app in a
    // stuck state.
    ReleaseSnapTapVirtualKeys_Locked();

    // Restore physical holds immediately (otherwise the user must re-press).
    if (!spamActive) {
      SendKeyDownForPhysicallyHeldWasd();
    }
  }

  if (spamActive) {
    PublishSpamKeysFromState_Locked(enabled);
  }
}

void RefreshMovementState() {
  const bool spamFeatureEnabled =
      Config::EnableSpam.load(std::memory_order_relaxed);
  const bool snapTapEnabled =
      Config::EnableSnapTap.load(std::memory_order_relaxed);
  const bool spamActive =
      Globals::g_isSpamActive.load(std::memory_order_relaxed) &&
      spamFeatureEnabled;

  std::lock_guard<std::mutex> lock(g_axisMutex);

  if (snapTapEnabled) {
    ApplySnapTapOutput_Locked(spamActive);
  } else {
    ReleaseSnapTapVirtualKeys_Locked();
  }

  if (spamActive) {
    PublishSpamKeysFromState_Locked(snapTapEnabled);
  }
}
