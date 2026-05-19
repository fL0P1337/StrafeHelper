// Application.cpp
#include "Application.h"
#include "AppWindow.h"
#include "Config.h"
#include "Globals.h"
#include "InputBackend.h"
#include "InterceptionBackend.h"
#include "KbdHookBackend.h"
#include "KeybindManager.h"
#include "MovementStateManager.h"
#include "SpamLogic.h"
#include "SuperglideLogic.h"
#include "TurboLogic.h"
#include "Utils.h"
#include <iostream>
#include <map>
#include <memory> // unique_ptr
#include <mutex>
#include <thread>
#include <vector>
#include <windows.h>

// --- Define Global Variables (declared extern in Globals.h) ---
namespace Globals {
HWND g_hWindow = NULL;
HINSTANCE g_hInstance = NULL;

// Need full types here since it's the definition
std::map<int, KeyState> g_KeyInfo;
std::vector<int> g_activeSpamKeys;
std::atomic<unsigned long long> g_spamKeysEpoch{0};
std::mutex g_activeKeysMutex;
std::atomic<bool> g_isCSpamActive{false};

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
// Shared keybind dispatch logic used by both interception keyboard events
// and the side-mouse hook path.
// -----------------------------------------------------------------------
bool HandleFeatureKeyEvent(int vkCode, bool isKeyDown) noexcept {
  auto itKeyInfo = Globals::g_KeyInfo.find(vkCode);
  if (itKeyInfo != Globals::g_KeyInfo.end()) {
    itKeyInfo->second.physicalKeyDown.store(isKeyDown,
                                            std::memory_order_relaxed);
  }

  const bool spamFeatureEnabled =
      Config::EnableSpam.load(std::memory_order_relaxed);
  const bool snapTapEnabled =
      Config::EnableSnapTap.load(std::memory_order_relaxed);
  const int currentTriggerKey =
      Config::KeySpamTrigger.load(std::memory_order_relaxed);

  // Trigger: gates macro spamming
  if (vkCode == currentTriggerKey && spamFeatureEnabled) {
    // Use KeybindManager to process hold/toggle mode
    const bool shouldBeActive =
        KeybindManager::ProcessSpamTrigger(vkCode, isKeyDown);

    if (shouldBeActive &&
        !Globals::g_isCSpamActive.load(std::memory_order_relaxed)) {
      Globals::g_isCSpamActive.store(true, std::memory_order_relaxed);
      std::cout << "Spam Activated" << std::endl;
      OnSpamActivated(snapTapEnabled);
    } else if (!shouldBeActive &&
               Globals::g_isCSpamActive.load(std::memory_order_relaxed)) {
      std::cout << "Spam Deactivated" << std::endl;
      OnSpamDeactivated(snapTapEnabled);
    }
    return false;
  }

  // Turbo Loot
  const int turboLootKey = Config::TurboLootKey.load(std::memory_order_relaxed);
  if (vkCode == turboLootKey &&
      Config::EnableTurboLoot.load(std::memory_order_relaxed)) {
    KeybindManager::ProcessTurboLoot(vkCode, isKeyDown);
    TriggerTurboLoot();
    return false;
  }

  // Turbo Jump
  const int turboJumpKey = Config::TurboJumpKey.load(std::memory_order_relaxed);
  if (vkCode == turboJumpKey &&
      Config::EnableTurboJump.load(std::memory_order_relaxed)) {
    KeybindManager::ProcessTurboJump(vkCode, isKeyDown);
    TriggerTurboJump();
    return false;
  }

  // Superglide bind is suppressed so it never reaches the game.
  const int superglideBindVK =
      Config::SuperglideBind.load(std::memory_order_relaxed);
  if (vkCode == superglideBindVK &&
      Config::EnableSuperglide.load(std::memory_order_relaxed)) {
    if (KeybindManager::ProcessSuperglide(vkCode, isKeyDown)) {
      TriggerSuperglide();
    }
    return true;
  }

  const bool spamActive =
      Globals::g_isCSpamActive.load(std::memory_order_relaxed) &&
      spamFeatureEnabled;
  if (HandleMovementKeyState(vkCode, isKeyDown, !isKeyDown, spamActive,
                             snapTapEnabled)) {
    return true;
  }

  return false;
}

// -----------------------------------------------------------------------
// DispatchKeyEvent - translates a NEO_KEY_EVENT and feeds shared keybind logic.
// Returns true if the physical event should be suppressed.
// -----------------------------------------------------------------------
bool DispatchKeyEvent(const NEO_KEY_EVENT &evt) noexcept {
  // Ignore events we ourselves injected (same marker as KbdHookBackend uses)
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

  // Handle dynamic keybinding capture
  if (KeybindManager::HandleBind(static_cast<int>(vkCode), isKeyDown)) {
    return true; // Suppress
  }

  return HandleFeatureKeyEvent(static_cast<int>(vkCode), isKeyDown);
}

void HandleSideMouseButton(int vkCode, bool isDown) {
  if (!KeybindManager::HandleBind(vkCode, isDown)) {
    HandleFeatureKeyEvent(vkCode, isDown);
  }
}

// -----------------------------------------------------------------------
// SwitchBackend — callable from the GUI thread at runtime
// -----------------------------------------------------------------------
void SwitchBackend(Config::InputBackendKind kind) {
  std::lock_guard<std::mutex> lock(g_backendMutex);

  const auto current =
      static_cast<Config::InputBackendKind>(Config::SelectedBackend.load());
  if (current == kind && g_activeBackend) {
    return; // nothing to do
  }

  // Stop whichever backend is currently running
  if (g_activeBackend) {
    g_activeBackend->Shutdown();
    g_activeBackend.reset();
  }

  Config::SelectedBackend.store(static_cast<int>(kind));
  Config::SaveConfig();

  // Start the newly selected backend
  std::unique_ptr<InputBackend> backend;
  if (kind == Config::InputBackendKind::Interception) {
    backend = std::make_unique<InterceptionBackend>();
  } else {
    backend = std::make_unique<KbdHookBackend>();
  }

  backend->SetCallback(DispatchKeyEvent);
  if (!backend->Initialize()) {
    std::cout << "[SwitchBackend] Backend initialization failed. Falling back to KbdHook." << std::endl;
    // Fall back to KbdHook if the requested backend fails
    Config::SelectedBackend.store(static_cast<int>(Config::InputBackendKind::KbdHook));
    Config::SaveConfig();
    
    backend = std::make_unique<KbdHookBackend>();
    backend->SetCallback(DispatchKeyEvent);
    if (!backend->Initialize()) {
      std::cout << "[SwitchBackend] Fallback backend initialization failed!" << std::endl;
      return;
    }
  }

  g_activeBackend = std::move(backend);
  std::cout << "[SwitchBackend] Switched to " << g_activeBackend->Name() << std::endl;
}

bool GetActiveBackendStatus(BackendStatus &out) noexcept {
  std::lock_guard<std::mutex> lock(g_backendMutex);
  if (g_activeBackend) {
    return g_activeBackend->GetStatus(out);
  }
  return false;
}

bool InitializeApplication(HINSTANCE hInstance) {
  Globals::g_hInstance = hInstance;

  Config::LoadConfig();

  // Initialize keybind manager for edge detection
  KeybindManager::Initialize();

  // Window creation moved to GuiManager inside main.cpp, but tray icon might
  // still rely on it. We will keep CreateAppWindow as a hidden message window
  // for Tray/Internal events.
  if (!CreateAppWindow(hInstance)) {
    return false;
  }

  for (int vk = 1; vk <= 0xFE; ++vk) {
    Globals::g_KeyInfo[vk];
  }
  std::cout << "Key states initialized." << std::endl;

  // --- Start the selected input backend ---
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
    std::cout << "[InitializeApplication] Backend init failed. Falling back to KbdHook." << std::endl;
    Config::SelectedBackend.store(static_cast<int>(Config::InputBackendKind::KbdHook));
    backend = std::make_unique<KbdHookBackend>();
    backend->SetCallback(DispatchKeyEvent);
    if (!backend->Initialize()) {
      std::cout << "[InitializeApplication] Fallback backend init failed!" << std::endl;
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
    // Need to stop hook/backend thread cleanly
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

  std::cout << "Application Initialization Successful." << std::endl;
  return true;
}

void CleanupApplication() {
  std::cout << "--- Starting Application Cleanup ---" << std::endl;

  StopSpamThread();
  StopTurboLootThread();
  StopTurboJumpThread();
  StopSuperglideThread();

  // Stop whichever input backend is active
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
      std::cout << "Window class unregistered." << std::endl;
    }
  }

  Globals::g_hInstance = NULL;

  std::cout << "--- Application Cleanup Finished ---" << std::endl;
}
