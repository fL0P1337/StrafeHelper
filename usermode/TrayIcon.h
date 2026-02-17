#pragma once
#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <shellapi.h>

#include "Config.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

class TrayIcon {
public:
  enum class Command {
    ToggleEnabled,
    ToggleSnapTap,
    ReloadConfig,
    OpenConfigFolder,
    SwitchInterception,
    SwitchKbdHook,
    SwitchRawInput,
    Exit
  };
  using CommandCallback = std::function<void(Command)>;

  TrayIcon() noexcept = default;
  ~TrayIcon() noexcept;

  [[nodiscard]] bool Initialize(HINSTANCE instance, bool enabled,
                                InputBackendKind backend,
                                CommandCallback callback) noexcept;
  void Shutdown() noexcept;
  void SetEnabled(bool enabled) noexcept;
  void SetSnapTap(bool enabled) noexcept;
  void SetBackend(InputBackendKind backend) noexcept;

private:
  static LRESULT CALLBACK WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam,
                                        LPARAM lParam) noexcept;
  LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) noexcept;
  void ShowContextMenu() noexcept;
  void UpdateTooltip() noexcept;
  void Invoke(Command command) noexcept;
  void ThreadMain() noexcept;

private:
  static constexpr UINT WM_TRAYICON = WM_APP + 1;
  static constexpr UINT WM_TRAY_SET_ENABLED = WM_APP + 2;
  static constexpr UINT WM_TRAY_SET_BACKEND = WM_APP + 3;
  static constexpr UINT WM_TRAY_SET_SNAPTAP = WM_APP + 4;

  static constexpr UINT ID_MENU_TOGGLE = 1001;
  static constexpr UINT ID_MENU_TOGGLE_SNAPTAP = 1002;
  static constexpr UINT ID_MENU_RELOAD = 1003;
  static constexpr UINT ID_MENU_OPEN_CONFIG = 1004;
  static constexpr UINT ID_MENU_BACKEND_INTERCEPTION = 1005;
  static constexpr UINT ID_MENU_BACKEND_KBDHOOK = 1006;
  static constexpr UINT ID_MENU_BACKEND_RAWINPUT = 1007;
  static constexpr UINT ID_MENU_EXIT = 1008;

  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
  HMENU menu_ = nullptr;
  NOTIFYICONDATAW nid_{};
  CommandCallback callback_{};

  std::thread thread_;
  std::atomic<DWORD> threadId_{0};
  std::atomic<bool> initialized_{false};

  std::mutex initMutex_;
  std::condition_variable initCv_;
  bool initDone_ = false;
  bool initOk_ = false;

  std::atomic<bool> enabled_{true};
  std::atomic<bool> snaptap_{true};
  std::atomic<InputBackendKind> backend_{InputBackendKind::Interception};
};
