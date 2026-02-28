// KeyboardHook.cpp
#include "KeyboardHook.h"
#include "Config.h"
#include "Globals.h" // <-- Add
#include "KeybindManager.h"
#include "SpamLogic.h"
#include "Utils.h"
#include <algorithm> // <-- Add
#include <iostream>
#include <vector>
#include <windows.h> // <-- Add (needed for KBDLLHOOKSTRUCT etc)

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
  std::vector<int> physicallyHeld; // press order (LIFO last)
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

bool AxisContains(const AxisState &axis, int vk) {
  return (std::find(axis.physicallyHeld.begin(), axis.physicallyHeld.end(),
                    vk) != axis.physicallyHeld.end());
}

// Returns true only for a real up->down transition (ignores autorepeat).
bool AxisOnKeyDown(AxisState &axis, int vk) {
  if (!AxisContains(axis, vk)) {
    axis.physicallyHeld.push_back(vk);
    axis.activeKey = vk;
    return true;
  }
  return false;
}

bool AxisOnKeyUp(AxisState &axis, int vk) {
  auto it =
      std::find(axis.physicallyHeld.begin(), axis.physicallyHeld.end(), vk);
  if (it == axis.physicallyHeld.end()) {
    return false;
  }

  axis.physicallyHeld.erase(it);
  axis.activeKey = axis.physicallyHeld.empty() ? 0 : axis.physicallyHeld.back();
  return true;
}

void SendKeyDownImmediate(int vkCode) {
  INPUT input = {INPUT_KEYBOARD};
  input.ki.wVk = vkCode;
  input.ki.wScan = MapVirtualKey(vkCode, MAPVK_VK_TO_VSC);
  input.ki.dwFlags = KEYEVENTF_SCANCODE;
  input.ki.dwExtraInfo = GetMessageExtraInfo();
  SendInput(1, &input, sizeof(INPUT));
}

void SendKeyUpImmediate(int vkCode) {
  INPUT input = {INPUT_KEYBOARD};
  input.ki.wVk = vkCode;
  input.ki.wScan = MapVirtualKey(vkCode, MAPVK_VK_TO_VSC);
  input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
  input.ki.dwExtraInfo = GetMessageExtraInfo();
  SendInput(1, &input, sizeof(INPUT));
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

  EnterCriticalSection(&Globals::g_csActiveKeys);
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
  LeaveCriticalSection(&Globals::g_csActiveKeys);

  if (changed) {
    if (Globals::g_hSpamEvent) {
      SetEvent(Globals::g_hSpamEvent);
    }
  }
}

void SendKeyUpForPhysicallyHeldWasd() {
  std::vector<INPUT> keyUpInputs;
  keyUpInputs.reserve(4);

  for (int key : {'W', 'A', 'S', 'D'}) {
    if (Globals::g_KeyInfo[key].physicalKeyDown.load(
            std::memory_order_relaxed)) {
      INPUT input = {INPUT_KEYBOARD};
      input.ki.wVk = key;
      input.ki.wScan = MapVirtualKey(key, MAPVK_VK_TO_VSC);
      input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
      input.ki.dwExtraInfo = GetMessageExtraInfo();
      keyUpInputs.push_back(input);
    }
  }

  if (!keyUpInputs.empty()) {
    SendInput(static_cast<UINT>(keyUpInputs.size()), keyUpInputs.data(),
              sizeof(INPUT));
  }
}

void SendKeyDownForPhysicallyHeldWasd() {
  std::vector<INPUT> keyDownInputs;
  keyDownInputs.reserve(4);

  for (int key : {'W', 'A', 'S', 'D'}) {
    if (Globals::g_KeyInfo[key].physicalKeyDown.load(
            std::memory_order_relaxed)) {
      INPUT input = {INPUT_KEYBOARD};
      input.ki.wVk = key;
      input.ki.wScan = MapVirtualKey(key, MAPVK_VK_TO_VSC);
      input.ki.dwFlags = KEYEVENTF_SCANCODE;
      input.ki.dwExtraInfo = GetMessageExtraInfo();
      keyDownInputs.push_back(input);
    }
  }

  if (!keyDownInputs.empty()) {
    SendInput(static_cast<UINT>(keyDownInputs.size()), keyDownInputs.data(),
              sizeof(INPUT));
  }
}

} // namespace

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode != HC_ACTION) {
    return CallNextHookEx(Globals::g_hHook, nCode, wParam, lParam); // Globals::
  }

  KBDLLHOOKSTRUCT *pKeybd = (KBDLLHOOKSTRUCT *)lParam;
  int vkCode = pKeybd->vkCode;
  bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
  bool isKeyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

  // Handle dynamic keybinding capture
  if (KeybindManager::HandleBind(vkCode, isKeyDown)) {
    return 1;
  }

  if (pKeybd->flags & LLKHF_INJECTED) {
    return CallNextHookEx(Globals::g_hHook, nCode, wParam, lParam); // Globals::
  }

  auto itKeyInfo = Globals::g_KeyInfo.find(vkCode);            // Globals::
  bool isManagedKey = (itKeyInfo != Globals::g_KeyInfo.end()); // Globals::

  const bool isWASD = IsWasdVk(vkCode);
  const int currentTriggerKey =
      Config::KeySpamTrigger.load(std::memory_order_relaxed);
  const bool isTrigger = (vkCode == currentTriggerKey);

  if (isManagedKey) {
    itKeyInfo->second.physicalKeyDown.store(isKeyDown,
                                            std::memory_order_relaxed);
  }

  const bool spamFeatureEnabled =
      Config::EnableSpam.load(std::memory_order_relaxed);
  const bool snapTapEnabled =
      Config::EnableSnapTap.load(std::memory_order_relaxed);
  const bool spamActive =
      Globals::g_isCSpamActive.load(std::memory_order_relaxed) &&
      spamFeatureEnabled;

  // Trigger gates macro spamming ONLY. SnapTap ownership resolution runs
  // independently.
  if (isTrigger && spamFeatureEnabled) {
    // Use KeybindManager to process hold/toggle mode
    bool shouldBeActive = KeybindManager::ProcessSpamTrigger(vkCode, isKeyDown);
    
    if (shouldBeActive && !Globals::g_isCSpamActive.load(std::memory_order_relaxed)) {
      Globals::g_isCSpamActive.store(true, std::memory_order_relaxed);
      std::cout << "Spam Activated" << std::endl;

      // Ensure any held movement keys are virtually UP while spamming.
      ApplySnapTapOutput(true);
      SendKeyUpForPhysicallyHeldWasd();

      PublishSpamKeysFromState(snapTapEnabled);
    } else if (!shouldBeActive && Globals::g_isCSpamActive.load(std::memory_order_relaxed)) {
      std::cout << "Spam Deactivated" << std::endl;

      if (snapTapEnabled) {
        CleanupSpamState(false);
        ApplySnapTapOutput(false);
      } else {
        CleanupSpamState(true);
      }
    }
    return CallNextHookEx(Globals::g_hHook, nCode, wParam, lParam);
  }

  if (isWASD) {
    AxisState *axis = AxisForVk(vkCode);
    if (axis) {
      if (isKeyDown) {
        AxisOnKeyDown(*axis, vkCode);
      } else if (isKeyUp) {
        AxisOnKeyUp(*axis, vkCode);
      }
    }

    if (spamActive) {
      // Spam layer keeps movement keys virtually UP and spams the active
      // key(s).
      ApplySnapTapOutput(true);
      SendKeyUpImmediate(vkCode);
      PublishSpamKeysFromState(snapTapEnabled);
      return 1; // swallow physical WASD while spamming
    }

    if (snapTapEnabled) {
      // SnapTap is always-on (even without trigger). It owns WASD output when
      // enabled.
      ApplySnapTapOutput(false);
      return 1; // swallow physical WASD and emit sanitized output via SendInput
    }
  }

  // --- Turbo key detection ---
  const int turboLootKey = Config::TurboLootKey.load(std::memory_order_relaxed);
  if (vkCode == turboLootKey &&
      Config::EnableTurboLoot.load(std::memory_order_relaxed)) {
    KeybindManager::ProcessTurboLoot(vkCode, isKeyDown);
    if (Globals::g_hTurboLootEvent)
      SetEvent(Globals::g_hTurboLootEvent);
  }

  const int turboJumpKey = Config::TurboJumpKey.load(std::memory_order_relaxed);
  if (vkCode == turboJumpKey &&
      Config::EnableTurboJump.load(std::memory_order_relaxed)) {
    KeybindManager::ProcessTurboJump(vkCode, isKeyDown);
    if (Globals::g_hTurboJumpEvent)
      SetEvent(Globals::g_hTurboJumpEvent);
  }

  // Superglide bind — swallow the physical key so it never reaches the game
  const int superglideBindVK =
      Config::SuperglideBind.load(std::memory_order_relaxed);
  if (vkCode == superglideBindVK &&
      Config::EnableSuperglide.load(std::memory_order_relaxed)) {
    // Use KeybindManager for hold/toggle mode (triggers on key-down edge)
    if (KeybindManager::ProcessSuperglide(vkCode, isKeyDown) && Globals::g_hSuperglideEvent)
      SetEvent(Globals::g_hSuperglideEvent);
    return 1; // suppress — do not pass to game
  }

  return CallNextHookEx(Globals::g_hHook, nCode, wParam, lParam); // Globals::
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

bool SetupKeyboardHook(HINSTANCE hInstance) {
  // Use Globals::g_hInstance if Application::InitializeApplication sets it
  // first
  Globals::g_hHook =
      SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, Globals::g_hInstance,
                       0); // Globals:: (both)
  if (!Globals::g_hHook) { // Globals::
    LogError("SetWindowsHookEx failed");
    return false;
  }
  std::cout << "Keyboard hook installed." << std::endl;
  return true;
}

void TeardownKeyboardHook() {
  if (Globals::g_hHook) {                        // Globals::
    if (UnhookWindowsHookEx(Globals::g_hHook)) { // Globals::
      std::cout << "Keyboard hook uninstalled." << std::endl;
    } else {
      LogError("UnhookWindowsHookEx failed");
    }
    Globals::g_hHook = NULL; // Globals::
  }
}
