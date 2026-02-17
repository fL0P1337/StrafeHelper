#define WIN32_LEAN_AND_MEAN
#include "TrayIcon.h"

#include <objbase.h>

#include <cwchar>
#include <utility>

namespace {
constexpr wchar_t kWindowClassName[] = L"NeoStrafeTrayWindowClass";

[[nodiscard]] const wchar_t *BackendLabel(InputBackendKind backend) {
  switch (backend) {
  case InputBackendKind::Interception:
    return L"Interception";
  case InputBackendKind::KeyboardHook:
    return L"Keyboard Hook";
  case InputBackendKind::RawInput:
    return L"RawInput";
  default:
    return L"Interception";
  }
}
} // namespace

TrayIcon::~TrayIcon() noexcept { Shutdown(); }

bool TrayIcon::Initialize(HINSTANCE instance, bool enabled,
                          InputBackendKind backend,
                          CommandCallback callback) noexcept {
  if (initialized_.load(std::memory_order_relaxed)) {
    return true;
  }

  instance_ = instance;
  callback_ = std::move(callback);
  enabled_.store(enabled, std::memory_order_relaxed);
  snaptap_.store(
      true, std::memory_order_relaxed); // Default to true, will be set by app
  backend_.store(backend, std::memory_order_relaxed);

  {
    std::lock_guard<std::mutex> lock(initMutex_);
    initDone_ = false;
    initOk_ = false;
  }

  thread_ = std::thread([this]() { ThreadMain(); });

  std::unique_lock<std::mutex> lock(initMutex_);
  initCv_.wait(lock, [&]() { return initDone_; });
  if (!initOk_) {
    lock.unlock();
    Shutdown();
    return false;
  }

  initialized_.store(true, std::memory_order_relaxed);
  return true;
}

void TrayIcon::Shutdown() noexcept {
  initialized_.store(false, std::memory_order_relaxed);

  const DWORD tid = threadId_.load(std::memory_order_relaxed);
  if (tid != 0) {
    (void)PostThreadMessageW(tid, WM_QUIT, 0, 0);
  }

  if (thread_.joinable()) {
    thread_.join();
  }

  hwnd_ = nullptr;
  menu_ = nullptr;
  threadId_.store(0, std::memory_order_relaxed);
}

void TrayIcon::SetEnabled(bool enabled) noexcept {
  enabled_.store(enabled, std::memory_order_relaxed);
  if (hwnd_) {
    (void)PostMessageW(hwnd_, WM_TRAY_SET_ENABLED, enabled ? 1u : 0u, 0);
  }
}

void TrayIcon::SetSnapTap(bool enabled) noexcept {
  snaptap_.store(enabled, std::memory_order_relaxed);
  if (hwnd_) {
    (void)PostMessageW(hwnd_, WM_TRAY_SET_SNAPTAP, enabled ? 1u : 0u, 0);
  }
}

void TrayIcon::SetBackend(InputBackendKind backend) noexcept {
  backend_.store(backend, std::memory_order_relaxed);
  if (hwnd_) {
    (void)PostMessageW(hwnd_, WM_TRAY_SET_BACKEND, static_cast<WPARAM>(backend),
                       0);
  }
}

LRESULT CALLBACK TrayIcon::WndProcStatic(HWND hwnd, UINT msg, WPARAM wParam,
                                         LPARAM lParam) noexcept {
  TrayIcon *self =
      reinterpret_cast<TrayIcon *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (msg == WM_NCCREATE) {
    const auto *cs = reinterpret_cast<const CREATESTRUCTW *>(lParam);
    self = static_cast<TrayIcon *>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  }

  if (!self) {
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }

  return self->WndProc(hwnd, msg, wParam, lParam);
}

LRESULT TrayIcon::WndProc(HWND hwnd, UINT msg, WPARAM wParam,
                          LPARAM lParam) noexcept {
  switch (msg) {
  case WM_TRAYICON:
    switch (LOWORD(lParam)) {
    case WM_LBUTTONUP:
      Invoke(Command::ToggleEnabled);
      return 0;
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
      ShowContextMenu();
      return 0;
    default:
      break;
    }
    return 0;

  case WM_TRAY_SET_ENABLED:
    enabled_.store(wParam != 0, std::memory_order_relaxed);
    UpdateTooltip();
    nid_.uFlags = NIF_TIP;
    (void)Shell_NotifyIconW(NIM_MODIFY, &nid_);
    return 0;

  case WM_TRAY_SET_SNAPTAP:
    snaptap_.store(wParam != 0, std::memory_order_relaxed);
    UpdateTooltip();
    nid_.uFlags = NIF_TIP;
    (void)Shell_NotifyIconW(NIM_MODIFY, &nid_);
    return 0;

  case WM_TRAY_SET_BACKEND:
    backend_.store(static_cast<InputBackendKind>(wParam),
                   std::memory_order_relaxed);
    UpdateTooltip();
    nid_.uFlags = NIF_TIP;
    (void)Shell_NotifyIconW(NIM_MODIFY, &nid_);
    return 0;

  case WM_COMMAND:
    switch (LOWORD(wParam)) {
    case ID_MENU_TOGGLE:
      Invoke(Command::ToggleEnabled);
      return 0;
    case ID_MENU_TOGGLE_SNAPTAP:
      Invoke(Command::ToggleSnapTap);
      return 0;
    case ID_MENU_RELOAD:
      Invoke(Command::ReloadConfig);
      return 0;
    case ID_MENU_OPEN_CONFIG:
      Invoke(Command::OpenConfigFolder);
      return 0;
    case ID_MENU_BACKEND_INTERCEPTION:
      Invoke(Command::SwitchInterception);
      return 0;
    case ID_MENU_BACKEND_KBDHOOK:
      Invoke(Command::SwitchKbdHook);
      return 0;
    case ID_MENU_BACKEND_RAWINPUT:
      Invoke(Command::SwitchRawInput);
      return 0;
    case ID_MENU_EXIT:
      Invoke(Command::Exit);
      return 0;
    default:
      break;
    }
    break;

  default:
    break;
  }

  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void TrayIcon::ShowContextMenu() noexcept {
  if (!menu_ || !hwnd_) {
    return;
  }

  while (GetMenuItemCount(menu_) > 0) {
    RemoveMenu(menu_, 0, MF_BYPOSITION);
  }

  // Enabled Toggle
  const bool isEnabled = enabled_.load(std::memory_order_relaxed);
  AppendMenuW(menu_, MF_STRING | (isEnabled ? MF_CHECKED : MF_UNCHECKED),
              ID_MENU_TOGGLE, L"Enabled");

  // SnapTap Toggle
  const bool isSnapTap = snaptap_.load(std::memory_order_relaxed);
  AppendMenuW(menu_, MF_STRING | (isSnapTap ? MF_CHECKED : MF_UNCHECKED),
              ID_MENU_TOGGLE_SNAPTAP, L"SnapTap");

  AppendMenuW(menu_, MF_SEPARATOR, 0, nullptr);

  // Backend Selection
  const InputBackendKind active = backend_.load(std::memory_order_relaxed);
  HMENU backendMenu = CreatePopupMenu();
  AppendMenuW(backendMenu,
              MF_STRING |
                  ((active == InputBackendKind::Interception) ? MF_CHECKED
                                                              : MF_UNCHECKED),
              ID_MENU_BACKEND_INTERCEPTION, L"Interception");
  AppendMenuW(backendMenu,
              MF_STRING |
                  ((active == InputBackendKind::KeyboardHook) ? MF_CHECKED
                                                              : MF_UNCHECKED),
              ID_MENU_BACKEND_KBDHOOK, L"Keyboard Hook");
  AppendMenuW(
      backendMenu,
      MF_STRING |
          ((active == InputBackendKind::RawInput) ? MF_CHECKED : MF_UNCHECKED),
      ID_MENU_BACKEND_RAWINPUT, L"RawInput");

  AppendMenuW(menu_, MF_POPUP, reinterpret_cast<UINT_PTR>(backendMenu),
              L"Backend");

  AppendMenuW(menu_, MF_SEPARATOR, 0, nullptr);

  AppendMenuW(menu_, MF_STRING, ID_MENU_RELOAD, L"Reload config");
  AppendMenuW(menu_, MF_STRING, ID_MENU_OPEN_CONFIG, L"Open config folder");

  AppendMenuW(menu_, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu_, MF_STRING, ID_MENU_EXIT, L"Exit");

  POINT pt{};
  GetCursorPos(&pt);
  SetForegroundWindow(hwnd_);
  (void)TrackPopupMenu(menu_, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0,
                       hwnd_, nullptr);
  PostMessageW(hwnd_, WM_NULL, 0, 0);

  // CreatePopupMenu returns a menu that isn't automatically destroyed if
  // attached (?) TrackPopupMenu doesn't destroy the passed menu 'menu_', but
  // 'backendMenu' is a submenu. Windows destroys submenus when the parent is
  // destroyed? Actually AppendMenuW with MF_POPUP transfers ownership? Yes.
}

void TrayIcon::UpdateTooltip() noexcept {
  const bool enabled = enabled_.load(std::memory_order_relaxed);
  const bool snaptap = snaptap_.load(std::memory_order_relaxed);
  const InputBackendKind backend = backend_.load(std::memory_order_relaxed);

  wchar_t text[128] = {};
  const wchar_t *state = enabled ? L"ENABLED" : L"DISABLED";
  const wchar_t *stState = snaptap ? L"ON" : L"OFF";
  const wchar_t *backendLabel = BackendLabel(backend);

  // Format:
  // NeoStrafe
  // Backend: Interception
  // SnapTap: ON
  // Status: ENABLED

  (void)swprintf_s(text, L"NeoStrafe\nBackend: %ls\nSnapTap: %ls\nStatus: %ls",
                   backendLabel, stState, state);
  (void)wcsncpy_s(nid_.szTip, _countof(nid_.szTip), text, _TRUNCATE);
}

void TrayIcon::Invoke(Command command) noexcept {
  if (callback_) {
    callback_(command);
  }
}

void TrayIcon::ThreadMain() noexcept {
  const HRESULT comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool comInitialized = SUCCEEDED(comHr);

  threadId_.store(GetCurrentThreadId(), std::memory_order_relaxed);
  MSG queueWarmup{};
  PeekMessageW(&queueWarmup, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = TrayIcon::WndProcStatic;
  wc.hInstance = instance_;
  wc.lpszClassName = kWindowClassName;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  (void)RegisterClassExW(&wc);

  hwnd_ =
      CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClassName, L"NeoStrafeTray",
                      WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance_, this);

  bool ok = (hwnd_ != nullptr);
  if (ok) {
    ShowWindow(hwnd_, SW_HIDE);

    menu_ = CreatePopupMenu();
    ok = (menu_ != nullptr);

    if (ok) {
      nid_ = {};
      nid_.cbSize = sizeof(nid_);
      nid_.hWnd = hwnd_;
      nid_.uID = 1;
      nid_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
      nid_.uCallbackMessage = WM_TRAYICON;
      nid_.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
      UpdateTooltip();

      ok = Shell_NotifyIconW(NIM_ADD, &nid_) == TRUE;
      if (ok) {
        nid_.uVersion = NOTIFYICON_VERSION_4;
        (void)Shell_NotifyIconW(NIM_SETVERSION, &nid_);
      }
    }
  }

  {
    std::lock_guard<std::mutex> lock(initMutex_);
    initOk_ = ok;
    initDone_ = true;
  }
  initCv_.notify_one();

  if (!ok) {
    if (menu_) {
      DestroyMenu(menu_);
      menu_ = nullptr;
    }
    if (hwnd_) {
      DestroyWindow(hwnd_);
      hwnd_ = nullptr;
    }
    if (comInitialized) {
      CoUninitialize();
    }
    threadId_.store(0, std::memory_order_relaxed);
    return;
  }

  MSG msg{};
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  (void)Shell_NotifyIconW(NIM_DELETE, &nid_);

  if (menu_) {
    DestroyMenu(menu_);
    menu_ = nullptr;
  }

  if (hwnd_) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }

  if (comInitialized) {
    CoUninitialize();
  }

  threadId_.store(0, std::memory_order_relaxed);
}
