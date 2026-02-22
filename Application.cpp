// Application.cpp
#include "Application.h"
#include "AppWindow.h"
#include "Config.h"
#include "Globals.h"
#include "InputBackend.h"
#include "InterceptionBackend.h"
#include "KeyboardHook.h"
#include "SpamLogic.h"
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
HINSTANCE g_hInstance = NULL;

// Need full types here since it's the definition
std::map<int, KeyState> g_KeyInfo;
std::vector<int> g_activeSpamKeys;
std::atomic<unsigned long long> g_spamKeysEpoch{0};
CRITICAL_SECTION g_csActiveKeys; // Initialized in InitializeApplication
std::atomic<bool> g_isCSpamActive{false};
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

// -----------------------------------------------------------------------
// DispatchKeyEvent — translates a NEO_KEY_EVENT into the same state updates
// that KeyboardProc handles for the WinHook backend.  Only accessed from
// the interception polling thread (single writer for g_KeyInfo atomics).
// -----------------------------------------------------------------------
void DispatchKeyEvent(const NEO_KEY_EVENT &evt) noexcept {
  // Ignore events we ourselves injected (same marker as KbdHookBackend uses)
  if (evt.nativeInformation == NEO_SYNTHETIC_INFORMATION) {
    return;
  }

  // Map scan code -> virtual key via Windows API
  const UINT vkCode = MapVirtualKeyW(evt.scanCode, MAPVK_VSC_TO_VK_EX);
  if (vkCode == 0) {
    return;
  }

  const bool isKeyDown = (evt.flags & NEO_KEY_BREAK) == 0u;
  const bool isKeyUp = !isKeyDown;

  // Update physicalKeyDown for managed keys
  auto itKeyInfo = Globals::g_KeyInfo.find(static_cast<int>(vkCode));
  if (itKeyInfo != Globals::g_KeyInfo.end()) {
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

  const int currentTriggerKey =
      Config::KeySpamTrigger.load(std::memory_order_relaxed);
  const bool isTrigger = (static_cast<int>(vkCode) == currentTriggerKey);

  // Trigger: gates macro spamming
  if (isTrigger && spamFeatureEnabled) {
    if (isKeyDown) {
      if (!Globals::g_isCSpamActive.load(std::memory_order_relaxed)) {
        Globals::g_isCSpamActive.store(true, std::memory_order_relaxed);
        std::cout << "Spam Activated" << std::endl;
        // let SpamLogic handle it via existing event mechanism
        if (Globals::g_hSpamEvent) {
          SetEvent(Globals::g_hSpamEvent);
        }
      }
    } else if (isKeyUp) {
      if (Globals::g_isCSpamActive.load(std::memory_order_relaxed)) {
        std::cout << "Spam Deactivated" << std::endl;
        CleanupSpamState(snapTapEnabled ? false : true);
      }
    }
    return;
  }

  // Turbo Loot
  const int turboLootKey = Config::TurboLootKey.load(std::memory_order_relaxed);
  if (static_cast<int>(vkCode) == turboLootKey &&
      Config::EnableTurboLoot.load(std::memory_order_relaxed)) {
    if (isKeyDown && Globals::g_hTurboLootEvent) {
      SetEvent(Globals::g_hTurboLootEvent);
    }
    return;
  }

  // Turbo Jump
  const int turboJumpKey = Config::TurboJumpKey.load(std::memory_order_relaxed);
  if (static_cast<int>(vkCode) == turboJumpKey &&
      Config::EnableTurboJump.load(std::memory_order_relaxed)) {
    if (isKeyDown && Globals::g_hTurboJumpEvent) {
      SetEvent(Globals::g_hTurboJumpEvent);
    }
    return;
  }

  // WASD + spam/snaptap: pass event through to existing backend via
  // SpamLogic's g_hSpamEvent if needed
  const bool isWASD =
      (vkCode == 'W' || vkCode == 'A' || vkCode == 'S' || vkCode == 'D');
  if (isWASD && spamActive) {
    // ensure SpamThread is awake with updated key state
    if (Globals::g_hSpamEvent) {
      SetEvent(Globals::g_hSpamEvent);
    }
  }
  (void)snapTapEnabled; // SnapTap visual output is handled by SpamThread
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
      // Pass the raw event back through the driver first (non-suppressing
      // backend)
      (void)g_interceptionBackend->PassThrough(batch[i]);
      DispatchKeyEvent(batch[i]);
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

  Globals::g_hSpamEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
  if (!Globals::g_hSpamEvent) {
    LogError("CreateEvent failed");
    DeleteCriticalSection(&Globals::g_csActiveKeys);
    return false;
  }

  Globals::g_hTurboLootEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
  Globals::g_hTurboJumpEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
  if (!Globals::g_hTurboLootEvent || !Globals::g_hTurboJumpEvent) {
    LogError("CreateEvent for turbo failed");
    if (Globals::g_hSpamEvent)
      CloseHandle(Globals::g_hSpamEvent);
    if (Globals::g_hTurboLootEvent)
      CloseHandle(Globals::g_hTurboLootEvent);
    if (Globals::g_hTurboJumpEvent)
      CloseHandle(Globals::g_hTurboJumpEvent);
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
  std::cout << "Key states initialized." << std::endl;

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
      if (Globals::g_hWindow)
        DestroyWindow(Globals::g_hWindow);
      UnregisterClass(Config::WINDOW_CLASS_NAME, Globals::g_hInstance);
      CloseHandle(Globals::g_hSpamEvent);
      CloseHandle(Globals::g_hTurboLootEvent);
      CloseHandle(Globals::g_hTurboJumpEvent);
      DeleteCriticalSection(&Globals::g_csActiveKeys);
      Globals::g_hSpamEvent = NULL;
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

    if (Globals::g_hWindow)
      DestroyWindow(Globals::g_hWindow);
    UnregisterClass(Config::WINDOW_CLASS_NAME, Globals::g_hInstance);
    CloseHandle(Globals::g_hSpamEvent);
    CloseHandle(Globals::g_hTurboLootEvent);
    CloseHandle(Globals::g_hTurboJumpEvent);
    DeleteCriticalSection(&Globals::g_csActiveKeys);
    Globals::g_hSpamEvent = NULL;
    return false;
  }

  StartTurboLootThread();
  StartTurboJumpThread();

  std::cout << "Application Initialization Successful." << std::endl;
  return true;
}

void CleanupApplication() {
  std::cout << "--- Starting Application Cleanup ---" << std::endl;

  StopSpamThread();
  StopTurboLootThread();
  StopTurboJumpThread();

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

  DeleteCriticalSection(&Globals::g_csActiveKeys);
  std::cout << "Critical section deleted." << std::endl;

  Globals::g_hInstance = NULL;

  std::cout << "--- Application Cleanup Finished ---" << std::endl;
}
