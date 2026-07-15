// Application.cpp
//
// Responsibilities:
//   - Defines all Globals:: variables (single definition rule).
//   - Owns the active InputBackend instance and its lifecycle mutex.
//   - Translates NEO_KEY_EVENT from the backend into VK events (DispatchKeyEvent).
//   - Handles raw mouse side-button input (HandleSideMouseButton).
//   - Manages application init / cleanup lifecycle.
//   - Provides InjectKey() as the single injection point for all feature threads.
//
// Feature routing logic lives in EventDispatcher.cpp — this file does not
// know about individual features (Spam, Turbo, Superglide, etc.).

#include "Application.h"
#include "AppWindow.h"
#include "Config.h"
#include "EventDispatcher.h"
#include "Globals.h"
#include "InputBackend.h"
#include "InterceptionBackend.h"
#include "KbdHookBackend.h"
#include "KeybindManager.h"
#include "Logger.h"
#include "MovementStateManager.h"
#include "SpamLogic.h"
#include "SuperglideLogic.h"
#include "TurboLogic.h"
#include "Utils.h"
#include <memory>
#include <mutex>
#include <windows.h>

// --- Define Global Variables (declared extern in Globals.h) ---
namespace Globals {
HWND g_hWindow = NULL;
HINSTANCE g_hInstance = NULL;

KeyState g_KeyInfo[256]; // Zero-initialized; atomics default to false.
std::atomic<uint32_t> g_lurchState{0};
std::atomic<bool> g_isSpamActive{false};

SuperglideStats g_superglideStats{};

// --- Binding State ---
std::atomic<std::atomic<int> *> g_bindingTarget{nullptr};

} // namespace Globals
// --- End Global Variable Definitions ---

namespace {
std::unique_ptr<InputBackend> g_activeBackend;
std::mutex g_backendMutex;
Config::InputBackendKind g_activeBackendKind = Config::InputBackendKind::KbdHook;
bool g_hasActiveBackendKind = false;
} // namespace

// -----------------------------------------------------------------------
// DispatchKeyEvent — registered as the backend's event callback.
// Translates a hardware-level NEO_KEY_EVENT into a VK code and delegates
// to HandleFeatureKeyEvent (EventDispatcher.cpp) for feature routing.
// Returns true if the physical event should be suppressed.
// -----------------------------------------------------------------------
bool DispatchKeyEvent(const NEO_KEY_EVENT &evt) noexcept {
  // Ignore events we ourselves injected (same marker as KbdHookBackend uses).
  if (evt.nativeInformation == NEO_SYNTHETIC_INFORMATION) {
    return false;
  }

  UINT scanCode = evt.scanCode;
  if ((evt.flags & NEO_KEY_E0) != 0u) {
    scanCode |= 0xE000u;
  } else if ((evt.flags & NEO_KEY_E1) != 0u) {
    scanCode |= 0xE100u;
  }
  const UINT vkCode = MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX);
  if (vkCode == 0) {
    return false;
  }

  const bool isKeyDown = (evt.flags & NEO_KEY_BREAK) == 0u;

  // Handle dynamic keybinding capture first; consumes the event if active.
  if (KeybindManager::HandleBind(static_cast<int>(vkCode), isKeyDown)) {
    return true;
  }

  return HandleFeatureKeyEvent(static_cast<int>(vkCode), isKeyDown);
}

// -----------------------------------------------------------------------
// HandleSideMouseButton — raw mouse input path for X1/X2 buttons.
// Called from AppWindow.cpp WM_INPUT handler.
// -----------------------------------------------------------------------
void HandleSideMouseButton(int vkCode, bool isDown) {
  if (!KeybindManager::HandleBind(vkCode, isDown)) {
    // Mouse side buttons are never suppressed — we only route them.
    (void)HandleFeatureKeyEvent(vkCode, isDown);
  }
}

// -----------------------------------------------------------------------
// SwitchBackend — callable from the GUI thread at runtime.
// -----------------------------------------------------------------------
bool SwitchBackend(Config::InputBackendKind kind) {
  {
    std::lock_guard<std::mutex> lock(g_backendMutex);
    if (g_activeBackend && g_hasActiveBackendKind &&
        g_activeBackendKind == kind) {
      return true;
    }
  }

  PrepareMovementStateForBackendSwitch();
  Config::KeySpamTriggerToggleActive.store(false, std::memory_order_relaxed);
  Config::TurboLootToggleActive.store(false, std::memory_order_relaxed);
  Config::TurboJumpToggleActive.store(false, std::memory_order_relaxed);

  std::unique_ptr<InputBackend> previousBackend;
  {
    std::lock_guard<std::mutex> lock(g_backendMutex);
    previousBackend = std::move(g_activeBackend);
    g_hasActiveBackendKind = false;
  }

  if (previousBackend) {
    previousBackend->Shutdown();
  }

  auto makeBackend = [](Config::InputBackendKind backendKind)
      -> std::unique_ptr<InputBackend> {
    if (backendKind == Config::InputBackendKind::Interception) {
      return std::make_unique<InterceptionBackend>();
    }
    return std::make_unique<KbdHookBackend>();
  };

  std::unique_ptr<InputBackend> backend = makeBackend(kind);
  backend->SetCallback(DispatchKeyEvent);
  if (!backend->Initialize()) {
    Logger::GetInstance().Log(
        "[SwitchBackend] Backend initialization failed. Falling back to KbdHook.");
    kind = Config::InputBackendKind::KbdHook;
    backend = makeBackend(kind);
    backend->SetCallback(DispatchKeyEvent);
    if (!backend->Initialize()) {
      Logger::GetInstance().Log(
          "[SwitchBackend] Fallback backend initialization failed!");
      return false;
    }
  }

  const std::string backendName = backend->Name();
  {
    std::lock_guard<std::mutex> lock(g_backendMutex);
    g_activeBackend = std::move(backend);
    g_activeBackendKind = kind;
    g_hasActiveBackendKind = true;
  }
  ReconcileMovementStateAfterBackendSwitch();
  TriggerSpamEvent();
  TriggerTurboLoot();
  TriggerTurboJump();
  Config::SelectedBackend.store(static_cast<int>(kind));
  Config::SaveConfig();
  Logger::GetInstance().Log("[SwitchBackend] Switched to " + backendName);
  return true;
}

bool GetActiveBackendStatus(BackendStatus &out) noexcept {
  std::lock_guard<std::mutex> lock(g_backendMutex);
  if (g_activeBackend) {
    return g_activeBackend->GetStatus(out);
  }
  return false;
}

// -----------------------------------------------------------------------
// InitializeApplication / CleanupApplication — top-level lifecycle.
// -----------------------------------------------------------------------
bool InitializeApplication(HINSTANCE hInstance) {
  Globals::g_hInstance = hInstance;

  Config::LoadConfig();
  KeybindManager::Initialize();

  // Create hidden message window (tray icon / raw mouse input).
  if (!CreateAppWindow(hInstance)) {
    return false;
  }

  // g_KeyInfo[256] is zero-initialized by default construction — no loop needed.
  Logger::GetInstance().Log("Key states initialized.");

  // Start the selected input backend.
  const auto backendKind =
      static_cast<Config::InputBackendKind>(Config::SelectedBackend.load());

  std::unique_ptr<InputBackend> backend;
  if (backendKind == Config::InputBackendKind::Interception) {
    backend = std::make_unique<InterceptionBackend>();
  } else {
    backend = std::make_unique<KbdHookBackend>();
  }

  backend->SetCallback(DispatchKeyEvent);
  if (!backend->Initialize()) {
    Logger::GetInstance().Log(
        "[InitializeApplication] Backend init failed. Falling back to KbdHook.");
    Config::SelectedBackend.store(
        static_cast<int>(Config::InputBackendKind::KbdHook));
    backend = std::make_unique<KbdHookBackend>();
    backend->SetCallback(DispatchKeyEvent);
    if (!backend->Initialize()) {
      Logger::GetInstance().Log(
          "[InitializeApplication] Fallback backend init failed!");
      if (Globals::g_hWindow)
        DestroyWindow(Globals::g_hWindow);
      UnregisterClass(Config::WINDOW_CLASS_NAME, Globals::g_hInstance);
      return false;
    }
  }

  {
    std::lock_guard<std::mutex> lock(g_backendMutex);
    g_activeBackend = std::move(backend);
    g_activeBackendKind = static_cast<Config::InputBackendKind>(
        Config::SelectedBackend.load(std::memory_order_relaxed));
    g_hasActiveBackendKind = true;
  }

  if (!StartSpamThread()) {
    std::unique_ptr<InputBackend> failedBackend;
    {
      std::lock_guard<std::mutex> lock(g_backendMutex);
      failedBackend = std::move(g_activeBackend);
      g_hasActiveBackendKind = false;
    }
    if (failedBackend) {
      failedBackend->Shutdown();
    }
    if (Globals::g_hWindow)
      DestroyWindow(Globals::g_hWindow);
    UnregisterClass(Config::WINDOW_CLASS_NAME, Globals::g_hInstance);
    return false;
  }

  StartTurboLootThread();
  StartTurboJumpThread();
  StartSuperglideThread();

  Logger::GetInstance().Log("Application Initialization Successful.");
  return true;
}

void CleanupApplication() {
  Logger::GetInstance().Log("--- Starting Application Cleanup ---");

  StopSpamThread();
  StopTurboLootThread();
  StopTurboJumpThread();
  StopSuperglideThread();

  std::unique_ptr<InputBackend> backend;
  {
    std::lock_guard<std::mutex> lock(g_backendMutex);
    backend = std::move(g_activeBackend);
    g_hasActiveBackendKind = false;
  }
  if (backend) {
    backend->Shutdown();
  }

  DestroyAppWindow();

  if (Globals::g_hInstance) {
    if (UnregisterClass(Config::WINDOW_CLASS_NAME, Globals::g_hInstance)) {
      Logger::GetInstance().Log("Window class unregistered.");
    }
  }

  Globals::g_hInstance = NULL;
  Logger::GetInstance().Log("--- Application Cleanup Finished ---");
}

// -----------------------------------------------------------------------
// InjectKey / InjectKeys — injection points used by all feature threads.
// Hold the backend mutex across each logical batch so runtime switching cannot
// split a batch across two backend instances.
// -----------------------------------------------------------------------
bool InjectKeys(const int *keys, std::size_t count, bool keyDown) noexcept {
  if (!keys || count == 0) {
    return true;
  }

  std::lock_guard<std::mutex> lock(g_backendMutex);
  if (!g_activeBackend) {
    return false;
  }

  bool allInjected = true;
  for (std::size_t i = 0; i < count; ++i) {
    uint16_t flags = keyDown ? 0 : NEO_KEY_BREAK;
    const UINT scanCode =
        MapVirtualKeyW(static_cast<UINT>(keys[i]), MAPVK_VK_TO_VSC_EX);
    if ((scanCode & 0xFF00u) == 0xE000u) {
      flags |= NEO_KEY_E0;
    }
    const uint16_t sc = static_cast<uint16_t>(scanCode & 0xFFu);
    if (sc == 0 || !g_activeBackend->InjectKey(sc, flags)) {
      allInjected = false;
    }
  }
  return allInjected;
}

bool InjectKey(int vk, bool keyDown) noexcept {
  return InjectKeys(&vk, 1, keyDown);
}
