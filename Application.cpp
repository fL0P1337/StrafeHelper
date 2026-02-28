// Application.cpp
#include "Application.h"
#include "AppWindow.h"
#include "Config.h"
#include "Globals.h"
#include "InputBackend.h"
#include "InterceptionBackend.h"
#include "KeybindManager.h"
#include "KeyboardHook.h"
#include "SpamLogic.h"
#include "SuperglideLogic.h"
#include "TurboLogic.h"
#include "Utils.h"
#include <iostream>
#include <map>    // <-- Add
#include <memory> // unique_ptr
#include <mutex>
#include <thread>
#include <vector> // <-- Add
#include <windows.h>

// --- Define Global Variables (declared extern in Globals.h) ---
namespace Globals {
HHOOK g_hHook = NULL;
HWND g_hWindow = NULL;
HANDLE g_hHookThread = NULL;
HANDLE g_hSpamThread = NULL;
HANDLE g_hSpamEvent = NULL;
HANDLE g_hTurboLootThread = NULL;
HANDLE g_hTurboLootEvent = NULL;
HANDLE g_hTurboJumpThread = NULL;
HANDLE g_hTurboJumpEvent = NULL;
HANDLE g_hSuperglideThread = NULL;
HANDLE g_hSuperglideEvent = NULL;
HINSTANCE g_hInstance = NULL;

// Need full types here since it's the definition
std::map<int, KeyState> g_KeyInfo;
std::vector<int> g_activeSpamKeys;
std::atomic<unsigned long long> g_spamKeysEpoch{0};
CRITICAL_SECTION g_csActiveKeys; // Initialized in InitializeApplication
std::atomic<bool> g_isCSpamActive{false};

SuperglideStats g_superglideStats{};

// --- Binding State ---
std::atomic<std::atomic<int>*> g_bindingTarget{nullptr};

} // namespace Globals
// --- End Global Variable Definitions ---

// -----------------------------------------------------------------------
// Interception backend state (only used when Interception is active)
// -----------------------------------------------------------------------
namespace {
std::unique_ptr<InputBackend> g_interceptionBackend;
std::thread g_interceptionThread;
std::atomic<bool> g_interceptionRunning{false};
std::mutex g_backendMutex; // guards start/stop of interception thread
std::thread g_sideMouseThread;
std::atomic<bool> g_sideMouseRunning{false};

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
      // let SpamLogic handle it via existing event mechanism
      if (Globals::g_hSpamEvent) {
        SetEvent(Globals::g_hSpamEvent);
      }
    } else if (!shouldBeActive &&
               Globals::g_isCSpamActive.load(std::memory_order_relaxed)) {
      std::cout << "Spam Deactivated" << std::endl;
      CleanupSpamState(snapTapEnabled ? false : true);
    }
    return false;
  }

  // Turbo Loot
  const int turboLootKey = Config::TurboLootKey.load(std::memory_order_relaxed);
  if (vkCode == turboLootKey &&
      Config::EnableTurboLoot.load(std::memory_order_relaxed)) {
    KeybindManager::ProcessTurboLoot(vkCode, isKeyDown);
    if (Globals::g_hTurboLootEvent)
      SetEvent(Globals::g_hTurboLootEvent);
    return false;
  }

  // Turbo Jump
  const int turboJumpKey = Config::TurboJumpKey.load(std::memory_order_relaxed);
  if (vkCode == turboJumpKey &&
      Config::EnableTurboJump.load(std::memory_order_relaxed)) {
    KeybindManager::ProcessTurboJump(vkCode, isKeyDown);
    if (Globals::g_hTurboJumpEvent)
      SetEvent(Globals::g_hTurboJumpEvent);
    return false;
  }

  // Superglide bind is suppressed so it never reaches the game.
  const int superglideBindVK =
      Config::SuperglideBind.load(std::memory_order_relaxed);
  if (vkCode == superglideBindVK &&
      Config::EnableSuperglide.load(std::memory_order_relaxed)) {
    if (KeybindManager::ProcessSuperglide(vkCode, isKeyDown) &&
        Globals::g_hSuperglideEvent) {
      SetEvent(Globals::g_hSuperglideEvent);
    }
    return true;
  }

  // Wake spam thread when movement keys change while spam is active.
  const bool spamActive =
      Globals::g_isCSpamActive.load(std::memory_order_relaxed) &&
      spamFeatureEnabled;
  const bool isWASD =
      (vkCode == 'W' || vkCode == 'A' || vkCode == 'S' || vkCode == 'D');
  if (isWASD && spamActive) {
    if (Globals::g_hSpamEvent) {
      SetEvent(Globals::g_hSpamEvent);
    }
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

  // Map scan code -> virtual key via Windows API
  const UINT vkCode = MapVirtualKeyW(evt.scanCode, MAPVK_VSC_TO_VK_EX);
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

// -----------------------------------------------------------------------
// Side-mouse polling thread (XBUTTON1/2) using GetAsyncKeyState
// This avoids using WH_MOUSE_LL hooks which cause cursor lag on 8000Hz mice.
// -----------------------------------------------------------------------
void SideMouseThreadFunc() noexcept {
  bool lastX1 = false;
  bool lastX2 = false;

  while (g_sideMouseRunning.load(std::memory_order_relaxed)) {
    // Check hardware state directly (MSB set = down)
    // 0x8000 is the high-order bit for "currently down"
    const bool x1 = (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) != 0;
    const bool x2 = (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) != 0;

    if (x1 != lastX1) {
      if (!KeybindManager::HandleBind(VK_XBUTTON1, x1)) {
        HandleFeatureKeyEvent(VK_XBUTTON1, x1);
      }
      lastX1 = x1;
    }

    if (x2 != lastX2) {
      if (!KeybindManager::HandleBind(VK_XBUTTON2, x2)) {
        HandleFeatureKeyEvent(VK_XBUTTON2, x2);
      }
      lastX2 = x2;
    }

    // Sleep briefly to yield CPU. 1-2ms is fine for human reaction time
    // and negligible CPU usage, while keeping cursor path 100% free.
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}

void StartSideMouseThread() {
  if (g_sideMouseRunning.load()) return;
  g_sideMouseRunning.store(true);
  g_sideMouseThread = std::thread(SideMouseThreadFunc);
  std::cout << "Side mouse polling thread started." << std::endl;
}

void StopSideMouseThread() {
  g_sideMouseRunning.store(false);
  if (g_sideMouseThread.joinable()) {
    g_sideMouseThread.join();
  }
}

// -----------------------------------------------------------------------
// Interception polling thread
// -----------------------------------------------------------------------
void InterceptionThreadMain() noexcept {
  constexpr uint32_t kPollTimeoutMs = 50;
  constexpr uint32_t kMaxBatch = 32;
  NEO_KEY_EVENT batch[kMaxBatch];

  while (g_interceptionRunning.load(std::memory_order_relaxed)) {
    if (!g_interceptionBackend) {
      break;
    }
    g_interceptionBackend->WaitForData(kPollTimeoutMs);

    const uint32_t count = g_interceptionBackend->PollEvents(batch, kMaxBatch);
    for (uint32_t i = 0; i < count; ++i) {
      const bool shouldSuppress = DispatchKeyEvent(batch[i]);
      if (!shouldSuppress) {
        (void)g_interceptionBackend->PassThrough(batch[i]);
      }
    }
  }
}

// -----------------------------------------------------------------------
// Stop the interception backend thread (call with g_backendMutex held or
// from CleanupApplication before destruction).
// -----------------------------------------------------------------------
void StopInterceptionBackend() {
  g_interceptionRunning.store(false, std::memory_order_relaxed);
  if (g_interceptionThread.joinable()) {
    g_interceptionThread.join();
  }
  if (g_interceptionBackend) {
    g_interceptionBackend->Shutdown();
    g_interceptionBackend.reset();
  }
}

// -----------------------------------------------------------------------
// Stop the existing WinHook thread
// -----------------------------------------------------------------------
void StopHookThread() {
  if (Globals::g_hHookThread) {
    PostThreadMessage(GetThreadId(Globals::g_hHookThread), WM_QUIT, 0, 0);
    WaitForSingleObject(Globals::g_hHookThread, 2000);
    CloseHandle(Globals::g_hHookThread);
    Globals::g_hHookThread = NULL;
  }
}
} // anonymous namespace

// -----------------------------------------------------------------------
// Original WinHook thread function — unchanged
// -----------------------------------------------------------------------
DWORD WINAPI HookThreadFunc(LPVOID /*lpParam*/) {
  if (!SetupKeyboardHook(Globals::g_hInstance)) {
    return 1;
  }

  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  TeardownKeyboardHook();
  return 0;
}

// -----------------------------------------------------------------------
// SwitchBackend — callable from the GUI thread at runtime
// -----------------------------------------------------------------------
void SwitchBackend(Config::InputBackendKind kind) {
  std::lock_guard<std::mutex> lock(g_backendMutex);

  const auto current =
      static_cast<Config::InputBackendKind>(Config::SelectedBackend.load());
  if (current == kind) {
    return; // nothing to do
  }

  // Stop whichever is currently running
  if (current == Config::InputBackendKind::Interception) {
    StopInterceptionBackend();
  } else {
    StopHookThread();
  }

  Config::SelectedBackend.store(static_cast<int>(kind));
  Config::SaveConfig();

  // Start the newly selected backend
  if (kind == Config::InputBackendKind::Interception) {
    auto backend = std::make_unique<InterceptionBackend>();
    if (!backend->Initialize()) {
      std::cout << "[SwitchBackend] Interception backend init failed. "
                   "Falling back to KbdHook."
                << std::endl;
      // revert
      Config::SelectedBackend.store(
          static_cast<int>(Config::InputBackendKind::KbdHook));
      Config::SaveConfig();
      Globals::g_hHookThread =
          CreateThread(NULL, 0, HookThreadFunc, NULL, 0, NULL);
      return;
    }
    g_interceptionBackend = std::move(backend);
    g_interceptionRunning.store(true, std::memory_order_relaxed);
    g_interceptionThread = std::thread(InterceptionThreadMain);
    std::cout << "[SwitchBackend] Switched to Interception backend."
              << std::endl;
  } else {
    Globals::g_hHookThread =
        CreateThread(NULL, 0, HookThreadFunc, NULL, 0, NULL);
    std::cout << "[SwitchBackend] Switched to KbdHook backend." << std::endl;
  }
}

bool InitializeApplication(HINSTANCE hInstance) {
  Globals::g_hInstance = hInstance;

  InitializeCriticalSection(&Globals::g_csActiveKeys);

  Config::LoadConfig();

  // Initialize keybind manager for edge detection
  KeybindManager::Initialize();

  Globals::g_hSpamEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
  if (!Globals::g_hSpamEvent) {
    LogError("CreateEvent failed");
    DeleteCriticalSection(&Globals::g_csActiveKeys);
    return false;
  }

  Globals::g_hTurboLootEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
  Globals::g_hTurboJumpEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
  Globals::g_hSuperglideEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
  if (!Globals::g_hTurboLootEvent || !Globals::g_hTurboJumpEvent ||
      !Globals::g_hSuperglideEvent) {
    LogError("CreateEvent for turbo/superglide failed");
    if (Globals::g_hSpamEvent)
      CloseHandle(Globals::g_hSpamEvent);
    if (Globals::g_hTurboLootEvent)
      CloseHandle(Globals::g_hTurboLootEvent);
    if (Globals::g_hTurboJumpEvent)
      CloseHandle(Globals::g_hTurboJumpEvent);
    if (Globals::g_hSuperglideEvent)
      CloseHandle(Globals::g_hSuperglideEvent);
    DeleteCriticalSection(&Globals::g_csActiveKeys);
    return false;
  }

  // Window creation moved to GuiManager inside main.cpp, but tray icon might
  // still rely on it. We will keep CreateAppWindow as a hidden message window
  // for Tray/Internal events.
  if (!CreateAppWindow(hInstance)) {
    CloseHandle(Globals::g_hSpamEvent);
    DeleteCriticalSection(&Globals::g_csActiveKeys);
    return false;
  }

  Globals::g_KeyInfo['W'];
  Globals::g_KeyInfo['A'];
  Globals::g_KeyInfo['S'];
  Globals::g_KeyInfo['D'];
  Globals::g_KeyInfo[VK_XBUTTON1];
  Globals::g_KeyInfo[VK_XBUTTON2];
  int triggerKey = Config::KeySpamTrigger.load();
  if (Globals::g_KeyInfo.find(triggerKey) == Globals::g_KeyInfo.end()) {
    Globals::g_KeyInfo[triggerKey];
  }
  // Register turbo keys
  int lootKey = Config::TurboLootKey.load();
  if (Globals::g_KeyInfo.find(lootKey) == Globals::g_KeyInfo.end()) {
    Globals::g_KeyInfo[lootKey];
  }
  int jumpKey = Config::TurboJumpKey.load();
  if (Globals::g_KeyInfo.find(jumpKey) == Globals::g_KeyInfo.end()) {
    Globals::g_KeyInfo[jumpKey];
  }
  int superglideKey = Config::SuperglideBind.load();
  if (Globals::g_KeyInfo.find(superglideKey) == Globals::g_KeyInfo.end()) {
    Globals::g_KeyInfo[superglideKey];
  }
  std::cout << "Key states initialized." << std::endl;

  StartSideMouseThread();

  // --- Start the selected input backend ---
  const auto backendKind =
      static_cast<Config::InputBackendKind>(Config::SelectedBackend.load());

  if (backendKind == Config::InputBackendKind::Interception) {
    auto backend = std::make_unique<InterceptionBackend>();
    if (backend->Initialize()) {
      g_interceptionBackend = std::move(backend);
      g_interceptionRunning.store(true, std::memory_order_relaxed);
      g_interceptionThread = std::thread(InterceptionThreadMain);
      std::cout << "Interception backend started." << std::endl;
    } else {
      std::cout << "[InitializeApplication] Interception backend init failed. "
                   "Falling back to KbdHook."
                << std::endl;
      Config::SelectedBackend.store(
          static_cast<int>(Config::InputBackendKind::KbdHook));
      // fall through to KbdHook
      Globals::g_hHookThread =
          CreateThread(NULL, 0, HookThreadFunc, NULL, 0, NULL);
      if (!Globals::g_hHookThread) {
        goto hook_thread_failed;
      }
    }
  } else {
    // Default: KbdHook
    Globals::g_hHookThread =
        CreateThread(NULL, 0, HookThreadFunc, NULL, 0, NULL);
    if (!Globals::g_hHookThread) {
    hook_thread_failed:
      StopSideMouseThread();
      if (Globals::g_hWindow)
        DestroyWindow(Globals::g_hWindow);
      UnregisterClass(Config::WINDOW_CLASS_NAME, Globals::g_hInstance);
      CloseHandle(Globals::g_hSpamEvent);
      CloseHandle(Globals::g_hTurboLootEvent);
      CloseHandle(Globals::g_hTurboJumpEvent);
      CloseHandle(Globals::g_hSuperglideEvent);
      DeleteCriticalSection(&Globals::g_csActiveKeys);
      Globals::g_hSpamEvent = NULL;
      Globals::g_hTurboLootEvent = NULL;
      Globals::g_hTurboJumpEvent = NULL;
      Globals::g_hSuperglideEvent = NULL;
      return false;
    }
  }

  if (!StartSpamThread()) {
    // Need to stop hook/backend thread cleanly
    if (Config::SelectedBackend.load() ==
        static_cast<int>(Config::InputBackendKind::Interception)) {
      StopInterceptionBackend();
    } else {
      StopHookThread();
    }

    StopSideMouseThread();

    if (Globals::g_hWindow)
      DestroyWindow(Globals::g_hWindow);
    UnregisterClass(Config::WINDOW_CLASS_NAME, Globals::g_hInstance);
    CloseHandle(Globals::g_hSpamEvent);
    CloseHandle(Globals::g_hTurboLootEvent);
    CloseHandle(Globals::g_hTurboJumpEvent);
    CloseHandle(Globals::g_hSuperglideEvent);
    DeleteCriticalSection(&Globals::g_csActiveKeys);
    Globals::g_hSpamEvent = NULL;
    Globals::g_hTurboLootEvent = NULL;
    Globals::g_hTurboJumpEvent = NULL;
    Globals::g_hSuperglideEvent = NULL;
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

  StopSideMouseThread();
  StopSpamThread();
  StopTurboLootThread();
  StopTurboJumpThread();
  StopSuperglideThread();

  // Stop whichever input backend is active
  {
    std::lock_guard<std::mutex> lock(g_backendMutex);
    if (Config::SelectedBackend.load() ==
        static_cast<int>(Config::InputBackendKind::Interception)) {
      StopInterceptionBackend();
    } else {
      StopHookThread();
    }
  }

  if (Globals::g_hInstance) {
    if (UnregisterClass(Config::WINDOW_CLASS_NAME, Globals::g_hInstance)) {
      std::cout << "Window class unregistered." << std::endl;
    }
  }

  if (Globals::g_hSpamEvent) {
    CloseHandle(Globals::g_hSpamEvent);
    Globals::g_hSpamEvent = NULL;
    std::cout << "Spam event handle closed." << std::endl;
  }
  if (Globals::g_hTurboLootEvent) {
    CloseHandle(Globals::g_hTurboLootEvent);
    Globals::g_hTurboLootEvent = NULL;
  }
  if (Globals::g_hTurboJumpEvent) {
    CloseHandle(Globals::g_hTurboJumpEvent);
    Globals::g_hTurboJumpEvent = NULL;
  }
  if (Globals::g_hSuperglideEvent) {
    CloseHandle(Globals::g_hSuperglideEvent);
    Globals::g_hSuperglideEvent = NULL;
  }

  DeleteCriticalSection(&Globals::g_csActiveKeys);
  std::cout << "Critical section deleted." << std::endl;

  Globals::g_hInstance = NULL;

  std::cout << "--- Application Cleanup Finished ---" << std::endl;
}
