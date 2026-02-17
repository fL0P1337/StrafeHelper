#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <atomic>
#include <cstdio>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <windows.h>

#include "Config.h"
#include "Engine.h"
#include "InputBackend.h"
#include "InterceptionBackend.h"
#include "KbdHookBackend.h"
#include "KeyNames.h"
#include "RawInputBackend.h"
#include "TrayIcon.h"
#include "Utils.h"

namespace {
std::atomic<bool> g_appRunning{true};
DWORD g_mainThreadId = 0;

BOOL WINAPI ConsoleHandler(DWORD ctrlType) {
  if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_CLOSE_EVENT) {
    g_appRunning.store(false, std::memory_order_relaxed);
    if (g_mainThreadId != 0) {
      (void)PostThreadMessageW(g_mainThreadId, WM_QUIT, 0, 0);
    }
    return TRUE;
  }
  return FALSE;
}

void InitializeWorkingDirectoryFromExe() noexcept {
  wchar_t exePath[MAX_PATH] = {};
  const DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) {
    return;
  }

  for (DWORD i = len; i > 0; --i) {
    if (exePath[i] == L'\\' || exePath[i] == L'/') {
      exePath[i] = L'\0';
      (void)SetCurrentDirectoryW(exePath);
      return;
    }
  }
}

[[nodiscard]] bool ValidateInterceptionDll() noexcept {
  HMODULE dll = LoadLibraryW(L"interception.dll");
  if (!dll) {
    Utils::LogError("interception.dll missing. NeoStrafeApp cannot start.");
    return false;
  }
  FreeLibrary(dll);
  return true;
}

[[nodiscard]] bool IsServiceRunning(SC_HANDLE scm,
                                    const wchar_t *serviceName) noexcept {
  SC_HANDLE service = OpenServiceW(scm, serviceName, SERVICE_QUERY_STATUS);
  if (!service) {
    Utils::LogError("OpenService(%ls) failed (err=%lu)", serviceName,
                    static_cast<unsigned long>(GetLastError()));
    return false;
  }

  SERVICE_STATUS status{};
  const BOOL ok = QueryServiceStatus(service, &status);
  const DWORD queryErr = GetLastError();
  CloseServiceHandle(service);

  if (!ok) {
    Utils::LogError("QueryServiceStatus(%ls) failed (err=%lu)", serviceName,
                    static_cast<unsigned long>(queryErr));
    return false;
  }

  return status.dwCurrentState == SERVICE_RUNNING;
}

[[nodiscard]] bool ValidateInterceptionServices() noexcept {
  SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
  if (!scm) {
    Utils::LogError("OpenSCManager failed (err=%lu)",
                    static_cast<unsigned long>(GetLastError()));
    return false;
  }

  const bool keyboardRunning = IsServiceRunning(scm, L"keyboard");
  const bool mouseRunning = IsServiceRunning(scm, L"mouse");
  CloseServiceHandle(scm);

  if (!keyboardRunning || !mouseRunning) {
    Utils::LogError("Interception driver not active");
    return false;
  }
  return true;
}

[[nodiscard]] std::unique_ptr<InputBackend>
CreateBackend(InputBackendKind kind) {
  switch (kind) {
  case InputBackendKind::Interception:
    return std::make_unique<InterceptionBackend>();
  case InputBackendKind::KeyboardHook:
    return std::make_unique<KbdHookBackend>();
  case InputBackendKind::RawInput:
    return std::make_unique<RawInputBackend>();
  default:
    return std::make_unique<InterceptionBackend>();
  }
}

[[nodiscard]] InputBackendKind PromptForBackend() {
  for (;;) {
    Utils::LogInfo("Select input backend:");
    Utils::LogInfo("1) Interception");
    Utils::LogInfo("2) Keyboard Hook");
    Utils::LogInfo("3) RawInput");
    std::printf("> ");

    std::string line;
    if (!std::getline(std::cin, line)) {
      return InputBackendKind::Interception;
    }

    if (line == "1") {
      return InputBackendKind::Interception;
    }
    if (line == "2") {
      return InputBackendKind::KeyboardHook;
    }
    if (line == "3") {
      return InputBackendKind::RawInput;
    }

    Utils::LogError("Invalid selection. Enter 1, 2, or 3.");
  }
}

[[nodiscard]] bool ValidateBackendPrereqs(InputBackendKind kind) {
  if (kind != InputBackendKind::Interception) {
    return true;
  }
  return ValidateInterceptionDll() && ValidateInterceptionServices();
}
} // namespace

int main() {
  g_mainThreadId = GetCurrentThreadId();
  InitializeWorkingDirectoryFromExe();
  SetConsoleCtrlHandler(ConsoleHandler, TRUE);

  MSG queueWarmup{};
  PeekMessageW(&queueWarmup, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

  RuntimeConfig config{};
  if (!Config::Load(config)) {
    Utils::LogError("Failed to initialize config.");
    return 1;
  }

  if (!config.hasInputBackend) {
    config.inputBackend = PromptForBackend();
    config.hasInputBackend = true;
    if (!Config::Save(config)) {
      Utils::LogError("Failed to persist selected input backend.");
      return 1;
    }
  }

  std::mutex runtimeMutex;
  std::unique_ptr<InputBackend> backend;
  std::unique_ptr<Engine> engine;
  std::jthread engineThread;
  std::atomic<bool> engineRunning{false};
  InputBackendKind activeBackend = config.inputBackend;

  const auto stopRuntime = [&]() {
    engineRunning.store(false, std::memory_order_relaxed);
    if (engineThread.joinable()) {
      engineThread.join();
    }
    if (backend) {
      backend->Shutdown();
    }
    engine.reset();
    backend.reset();
  };

  const auto startRuntime = [&](InputBackendKind kind) -> bool {
    if (!ValidateBackendPrereqs(kind)) {
      return false;
    }

    backend = CreateBackend(kind);
    if (!backend || !backend->Initialize()) {
      return false;
    }

    engine = std::make_unique<Engine>(*backend, config);
    engineRunning.store(true, std::memory_order_relaxed);
    engineThread =
        std::jthread([&](std::stop_token) { engine->Run(engineRunning); });
    activeBackend = kind;

    Utils::LogInfo("Backend: %s", backend->Name());
    Utils::LogInfo(
        "Trigger key: %s (0x%02X)",
        KeyNames::ScancodeToKeyName(engine->TriggerScanCode()).c_str(),
        engine->TriggerScanCode());
    Utils::LogInfo("Spam timing: down=%u us up=%u us", engine->SpamDownUs(),
                   engine->SpamUpUs());
    return true;
  };

  const auto restartRuntime = [&](InputBackendKind requested) -> bool {
    std::lock_guard<std::mutex> lock(runtimeMutex);

    const InputBackendKind old = activeBackend;
    stopRuntime();

    if (!startRuntime(requested)) {
      Utils::LogError("Failed to initialize backend '%s'.",
                      Config::BackendKindToString(requested));
      if (requested != old) {
        (void)startRuntime(old);
      }
      return false;
    }

    config.inputBackend = requested;
    config.hasInputBackend = true;
    if (!Config::Save(config)) {
      Utils::LogError("Failed to persist backend selection.");
    }

    return true;
  };

  if (!restartRuntime(config.inputBackend)) {
    return 1;
  }

  // Startup Banner
  std::printf("\n");
  std::printf("==================================================\n");
  std::printf("   NeoStrafe - Advanced Movement Utility\n");
  std::printf("==================================================\n");
  std::printf("   Trigger:    %s\n",
              KeyNames::ScancodeToKeyName(config.triggerScanCode).c_str());
  std::printf("   SnapTap:    %s\n", config.snaptapEnabled ? "ON" : "OFF");
  std::printf("   Backend:    %s\n",
              Config::BackendKindToString(activeBackend));
  std::printf("   Spam:       %u / %u us\n", config.spamDownUs,
              config.spamUpUs);
  std::printf("==================================================\n");
  std::printf("   Minimize to tray to hide this window.\n");
  std::printf("   Right-click tray icon for options.\n");
  std::printf("\n");

  TrayIcon tray;
  if (!tray.Initialize(
          GetModuleHandleW(nullptr), config.enabled, activeBackend,
          [&](TrayIcon::Command command) {
            if (command == TrayIcon::Command::ToggleEnabled) {
              std::lock_guard<std::mutex> lock(runtimeMutex);
              if (!engine) {
                return;
              }
              const bool enabled = engine->ToggleEnabled();
              config.enabled = enabled;
              tray.SetEnabled(enabled);
              (void)Config::Save(config);
              Utils::LogInfo("Macros %s", enabled ? "ENABLED" : "DISABLED");
              return;
            }

            if (command == TrayIcon::Command::ToggleSnapTap) {
              std::lock_guard<std::mutex> lock(runtimeMutex);
              config.snaptapEnabled = !config.snaptapEnabled;
              if (engine) {
                engine->ApplyConfig(config);
              }
              tray.SetSnapTap(config.snaptapEnabled);
              (void)Config::Save(config);
              Utils::LogInfo("SnapTap %s",
                             config.snaptapEnabled ? "ON" : "OFF");
              return;
            }

            if (command == TrayIcon::Command::OpenConfigFolder) {
              wchar_t exePath[MAX_PATH] = {};
              if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
                for (DWORD i = lstrlenW(exePath); i > 0; --i) {
                  if (exePath[i] == L'\\' || exePath[i] == L'/') {
                    exePath[i] = L'\0';
                    break;
                  }
                }
                ShellExecuteW(nullptr, L"open", exePath, nullptr, nullptr,
                              SW_SHOW);
              }
              return;
            }

            if (command == TrayIcon::Command::ReloadConfig) {
              RuntimeConfig reloaded{};
              if (!Config::Load(reloaded)) {
                Utils::LogError("Configuration reload failed.");
                return;
              }

              if (!reloaded.hasInputBackend) {
                reloaded.inputBackend = activeBackend;
                reloaded.hasInputBackend = true;
              }

              {
                std::lock_guard<std::mutex> lock(runtimeMutex);
                config = reloaded;
                if (engine) {
                  engine->ApplyConfig(config);
                  tray.SetEnabled(engine->IsEnabled());
                  tray.SetSnapTap(config.snaptapEnabled);
                }
              }

              if (reloaded.inputBackend != activeBackend) {
                (void)restartRuntime(reloaded.inputBackend);
              }

              tray.SetBackend(activeBackend);
              (void)Config::Save(config);
              Utils::LogInfo("Configuration reloaded.");
              // Update status prints
              Utils::LogInfo(
                  "Trigger: %s, SnapTap: %s",
                  KeyNames::ScancodeToKeyName(config.triggerScanCode).c_str(),
                  config.snaptapEnabled ? "ON" : "OFF");
              return;
            }

            if (command == TrayIcon::Command::SwitchInterception) {
              if (restartRuntime(InputBackendKind::Interception)) {
                tray.SetBackend(activeBackend);
              }
              return;
            }

            if (command == TrayIcon::Command::SwitchKbdHook) {
              if (restartRuntime(InputBackendKind::KeyboardHook)) {
                tray.SetBackend(activeBackend);
              }
              return;
            }

            if (command == TrayIcon::Command::SwitchRawInput) {
              if (restartRuntime(InputBackendKind::RawInput)) {
                tray.SetBackend(activeBackend);
              }
              return;
            }

            if (command == TrayIcon::Command::Exit) {
              g_appRunning.store(false, std::memory_order_relaxed);
              (void)PostThreadMessageW(g_mainThreadId, WM_QUIT, 0, 0);
            }
          })) {
    Utils::LogError("Failed to initialize tray icon.");
  }

  tray.SetSnapTap(config.snaptapEnabled);

  MSG msg{};
  while (g_appRunning.load(std::memory_order_relaxed)) {
    const BOOL result = GetMessageW(&msg, nullptr, 0, 0);
    if (result <= 0) {
      break;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  g_appRunning.store(false, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(runtimeMutex);
    stopRuntime();
  }
  tray.Shutdown();

  Utils::LogInfo("Clean exit.");
  return 0;
}
