// MovementStateManager.cpp
#include "MovementStateManager.h"
#include "Config.h"
#include "Globals.h"
#include "Application.h"
#include "KeybindManager.h"
#include "SpamLogic.h"
#include "Utils.h"
#include <mutex>
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
//
// Builds the WASD bitmask the spam thread should currently spam. With
// SnapTap on, that's the per-axis "winner" (LIFO last-pressed). With
// SnapTap off, every physically-held WASD key is spammed.
[[nodiscard]] uint32_t BuildDesiredSpamMask_Locked(bool snapTapEnabled) {
  uint32_t mask = 0u;

  if (snapTapEnabled) {
    mask |= Globals::LurchBitForVk(g_axisX.activeKey);
    mask |= Globals::LurchBitForVk(g_axisY.activeKey);
  } else {
    for (int key : {'W', 'A', 'S', 'D'}) {
      if (Globals::g_KeyInfo[key].physicalKeyDown.load(
              std::memory_order_acquire)) {
        mask |= Globals::LurchBitForVk(key);
      }
    }
  }
  return mask;
}

// NOTE: Must be called with g_axisMutex held.
//
// Atomically publishes the new desired mask into `g_lurchState`, bumping
// the epoch only when the key bits actually change. On a change, also
// updates the per-key `spamming` flag and signals the spam CV.
//
// All publish work is mutex-free: spammers see a single 32-bit atomic.
void PublishSpamKeysFromState_Locked(bool snapTapEnabled) {
  const uint32_t desiredKeys = BuildDesiredSpamMask_Locked(snapTapEnabled);

  uint32_t prev = Globals::g_lurchState.load(std::memory_order_acquire);
  uint32_t next = 0u;
  bool changed = false;
  do {
    const uint32_t prevKeys = prev & Globals::kLurchKeyMask;
    if (prevKeys == desiredKeys) {
      changed = false;
      next = prev;
      break;
    }
    const uint32_t epoch =
        (prev & ~Globals::kLurchKeyMask) + Globals::kLurchEpochInc;
    next = (epoch & ~Globals::kLurchKeyMask) | desiredKeys;
    changed = true;
  } while (!Globals::g_lurchState.compare_exchange_weak(
      prev, next, std::memory_order_acq_rel, std::memory_order_acquire));

  // Mirror the bitmask onto the per-key `spamming` flag. This isn't on
  // the spam thread's hot path — it's only used by GUI / cleanup logic.
  for (int key : {'W', 'A', 'S', 'D'}) {
    const bool spamming =
        (desiredKeys & Globals::LurchBitForVk(key)) != 0u;
    Globals::g_KeyInfo[key].spamming.store(spamming,
                                           std::memory_order_relaxed);
  }

  if (changed) {
    TriggerSpamEvent();
  }
}

void SendKeyUpForPhysicallyHeldWasd() {
  for (int key : {'W', 'A', 'S', 'D'}) {
    if (Globals::g_KeyInfo[key].physicalKeyDown.load(
            std::memory_order_acquire)) {
      InjectKey(key, false);
    }
  }
}

void SendKeyDownForPhysicallyHeldWasd() {
  for (int key : {'W', 'A', 'S', 'D'}) {
    if (Globals::g_KeyInfo[key].physicalKeyDown.load(
            std::memory_order_acquire)) {
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
      Globals::g_isSpamActive.load(std::memory_order_acquire) &&
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
      Globals::g_isSpamActive.load(std::memory_order_acquire) &&
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
