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
std::vector<int> g_activeSpamKeys;
std::atomic<unsigned long long> g_spamKeysEpoch{0};
std::mutex g_activeKeysMutex;
std::atomic<bool> g_isSpamActive{false};

SuperglideStats g_superglideStats{};

// --- Binding State ---
std::atomic<std::atomic<int> *> g_bindingTarget{nullptr};

} // namespace Globals
// --- End Global Variable Definitions ---

namespace {
std::unique_ptr<InputBackend> g_activeBackend;
std::mutex g_backendMutex;
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
void SwitchBackend(Config::InputBackendKind kind) {
  std::lock_guard<std::mutex> lock(g_backendMutex);

  const auto current =
      static_cast<Config::InputBackendKind>(Config::SelectedBackend.load());
  if (current == kind && g_activeBackend) {
    return; // Already running the requested backend.
  }

  if (g_activeBackend) {
    g_activeBackend->Shutdown();
    g_activeBackend.reset();
  }

  Config::SelectedBackend.store(static_cast<int>(kind));
  Config::SaveConfig();

  auto makeBackend = [&]() -> std::unique_ptr<InputBackend> {
    if (kind == Config::InputBackendKind::Interception) {
      return std::make_unique<InterceptionBackend>();
    }
    return std::make_unique<KbdHookBackend>();
  };

  std::unique_ptr<InputBackend> backend = makeBackend();
  backend->SetCallback(DispatchKeyEvent);
  if (!backend->Initialize()) {
    Logger::GetInstance().Log(
        "[SwitchBackend] Backend initialization failed. Falling back to KbdHook.");
    kind = Config::InputBackendKind::KbdHook;
    Config::SelectedBackend.store(static_cast<int>(kind));
    Config::SaveConfig();

    backend = std::make_unique<KbdHookBackend>();
    backend->SetCallback(DispatchKeyEvent);
    if (!backend->Initialize()) {
      Logger::GetInstance().Log(
          "[SwitchBackend] Fallback backend initialization failed!");
      return;
    }
  }

  g_activeBackend = std::move(backend);
  Logger::GetInstance().Log(
      std::string("[SwitchBackend] Switched to ") + g_activeBackend->Name());
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
  }

  if (!StartSpamThread()) {
    {
      std::lock_guard<std::mutex> lock(g_backendMutex);
      if (g_activeBackend) {
        g_activeBackend->Shutdown();
        g_activeBackend.reset();
      }
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

  {
    std::lock_guard<std::mutex> lock(g_backendMutex);
    if (g_activeBackend) {
      g_activeBackend->Shutdown();
      g_activeBackend.reset();
    }
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
// InjectKey — the single injection point used by all feature threads.
// Acquires the backend mutex, looks up the scan code, and calls the
// backend's InjectKey method.
// -----------------------------------------------------------------------
bool InjectKey(int vk, bool keyDown) noexcept {
  uint16_t flags = 0;
  if (!keyDown) {
    flags |= NEO_KEY_BREAK;
  }
  const UINT scanCode = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC_EX);
  if ((scanCode & 0xFF00u) == 0xE000u) {
    flags |= NEO_KEY_E0;
  }
  const uint16_t sc = static_cast<uint16_t>(scanCode & 0xFFu);

  std::lock_guard<std::mutex> lock(g_backendMutex);
  if (g_activeBackend) {
    return g_activeBackend->InjectKey(sc, flags);
  }
  return false;
}
