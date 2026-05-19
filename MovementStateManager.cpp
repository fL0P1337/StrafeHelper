// MovementStateManager.cpp
#include "MovementStateManager.h"
#include "Config.h"
#include "Globals.h"
#include "Application.h"
#include "KeybindManager.h"
#include "SpamLogic.h"
#include "Utils.h"
#include <algorithm>
#include <iostream>
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

void ApplyVirtualAxisState(int &currentVk, int desiredVk) {
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

void ReleaseSnapTapVirtualKeys() {
  ApplyVirtualAxisState(g_virtualAxisX, 0);
  ApplyVirtualAxisState(g_virtualAxisY, 0);
}

void ApplySnapTapOutput(bool spamActive) {
  const int desiredX = spamActive ? 0 : g_axisX.activeKey;
  const int desiredY = spamActive ? 0 : g_axisY.activeKey;

  ApplyVirtualAxisState(g_virtualAxisX, desiredX);
  ApplyVirtualAxisState(g_virtualAxisY, desiredY);
}

std::vector<int> BuildDesiredSpamKeys(bool snapTapEnabled) {
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

void PublishSpamKeysFromState(bool snapTapEnabled) {
  const std::vector<int> desiredKeys = BuildDesiredSpamKeys(snapTapEnabled);
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
  // Ensure any held movement keys are virtually UP while spamming.
  ApplySnapTapOutput(true);
  SendKeyUpForPhysicallyHeldWasd();
  PublishSpamKeysFromState(snapTapEnabled);
}

void OnSpamDeactivated(bool snapTapEnabled) {
  if (snapTapEnabled) {
    CleanupSpamState(false);
    ApplySnapTapOutput(false);
  } else {
    CleanupSpamState(true);
  }
}

bool HandleMovementKeyState(int vkCode, bool isKeyDown, bool isKeyUp,
                            bool spamActive, bool snapTapEnabled) {
  if (!IsWasdVk(vkCode)) {
    return false;
  }

  AxisState *axis = AxisForVk(vkCode);
  if (axis) {
    if (isKeyDown) {
      AxisOnKeyDown(*axis, vkCode);
    } else if (isKeyUp) {
      AxisOnKeyUp(*axis, vkCode);
    }
  }

  if (spamActive) {
    // Spam layer keeps movement keys virtually UP and spams the active key(s).
    ApplySnapTapOutput(true);
    SendKeyUpImmediate(vkCode);
    PublishSpamKeysFromState(snapTapEnabled);
    return true;
  }

  if (snapTapEnabled) {
    // SnapTap is always-on (even without trigger). It owns WASD output when
    // enabled.
    ApplySnapTapOutput(false);
    return true;
  }

  return false;
}

void OnSnapTapToggled(bool enabled) {
  const bool spamFeatureEnabled =
      Config::EnableSpam.load(std::memory_order_relaxed);
  const bool spamActive =
      Globals::g_isCSpamActive.load(std::memory_order_relaxed) &&
      spamFeatureEnabled;

  if (enabled) {
    // Clear any physical holds currently down in the target app before SnapTap
    // takes over.
    if (!spamActive) {
      SendKeyUpForPhysicallyHeldWasd();
    }
    ApplySnapTapOutput(spamActive);
  } else {
    // Release any SnapTap-owned keys so we don't leave the target app in a
    // stuck state.
    ReleaseSnapTapVirtualKeys();

    // Restore physical holds immediately (otherwise the user must re-press).
    if (!spamActive) {
      SendKeyDownForPhysicallyHeldWasd();
    }
  }

  if (spamActive) {
    PublishSpamKeysFromState(enabled);
  }
}

void RefreshMovementState() {
  const bool spamFeatureEnabled =
      Config::EnableSpam.load(std::memory_order_relaxed);
  const bool snapTapEnabled =
      Config::EnableSnapTap.load(std::memory_order_relaxed);
  const bool spamActive =
      Globals::g_isCSpamActive.load(std::memory_order_relaxed) &&
      spamFeatureEnabled;

  if (snapTapEnabled) {
    ApplySnapTapOutput(spamActive);
  } else {
    ReleaseSnapTapVirtualKeys();
  }

  if (spamActive) {
    PublishSpamKeysFromState(snapTapEnabled);
  }
}
