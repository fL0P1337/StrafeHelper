// main.cpp
#define WIN32_LEAN_AND_MEAN
#include "Application.h"
#include "Config.h"
#include "Globals.h"
#include "Logger.h"
#include "gui/GuiManager.h"
#include "imgui/imgui.h"
#include <dwmapi.h>
#include <string>
#include <windows.h>

#pragma comment(lib, "dwmapi.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

// Window geometry constants
static constexpr int kResizeBorder   = 6;  // px — hit-test resize border width
static constexpr int kDragBorderBot  = 30; // px — drag strip at window bottom
static constexpr int kTitleBarHeight = 51; // px — draggable title bar height
static constexpr int kLogoAreaRight  = 160; // px — logo drag zone right edge

static void EnableBorderlessResize(HWND hwnd) {
  // Extend the frame into the client area to remove the standard window frame.
  // This eliminates the white top border completely.
  MARGINS margins = {0, 0, 0, 1};
  DwmExtendFrameIntoClientArea(hwnd, &margins);

  LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
  style |= WS_THICKFRAME | WS_POPUP;
  style &= ~WS_CAPTION;
  SetWindowLongPtr(hwnd, GWL_STYLE, style);

  SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
               SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOMOVE | SWP_NOSIZE |
                   SWP_FRAMECHANGED);
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                             LPARAM lParam) {
  // Handle WM_NCCALCSIZE to remove the non-client area (white top border)
  if (msg == WM_NCCALCSIZE) {
    if (wParam) {
      return 0; // Entire window is client area
    }
  }

  if (msg == WM_NCHITTEST) {
    LRESULT hit = DefWindowProc(hwnd, msg, wParam, lParam);
    if (hit == HTCLIENT) {
      POINT pt = {LOWORD(lParam), HIWORD(lParam)};
      ScreenToClient(hwnd, &pt);
      RECT rc;
      GetClientRect(hwnd, &rc);

      const bool left   = pt.x < kResizeBorder;
      const bool right  = pt.x >= rc.right - kResizeBorder;
      const bool top    = pt.y < kResizeBorder;
      const bool bottom = pt.y >= rc.bottom - kResizeBorder;

      if (top && left)    return HTTOPLEFT;
      if (top && right)   return HTTOPRIGHT;
      if (bottom && left) return HTBOTTOMLEFT;
      if (bottom && right) return HTBOTTOMRIGHT;
      if (left)   return HTLEFT;
      if (right)  return HTRIGHT;
      if (top)    return HTTOP;
      if (bottom) return HTBOTTOM;

      if (pt.y > (rc.bottom - kDragBorderBot))
        return HTCAPTION;

      if (pt.y < kTitleBarHeight && pt.x < kLogoAreaRight)
        return HTCAPTION;
    }
    return hit;
  }

  if (Gui::GuiManager::GetInstance().WndProcHandler(hwnd, msg, wParam, lParam))
    return true;

  switch (msg) {
  case WM_SYSCOMMAND:
    if ((wParam & 0xfff0) == SC_KEYMENU)
      return 0;
    break;
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
  Logger::GetInstance().Log(std::string(Config::APP_NAME) + " v" +
                            Config::VERSION + " starting...");

  // Register UI Window Class
  WNDCLASSEXW wc = {
      sizeof(WNDCLASSEXW),     CS_CLASSDC, MainWndProc, 0L,   0L,
      GetModuleHandle(NULL),   NULL,       NULL,        NULL, NULL,
      L"StrafeHelperGUIClass", NULL};
  if (!::RegisterClassExW(&wc)) {
    MessageBoxA(nullptr, "Failed to register GUI window class.",
                "Initialization Error", MB_OK | MB_ICONERROR);
    return 1;
  }

  std::wstring windowTitle =
      std::wstring(Config::APP_NAME,
                   Config::APP_NAME + strlen(Config::APP_NAME)) +
      L" - Configuration";

  // Local handle for the GUI window.
  // NOTE: Globals::g_hWindow is intentionally NOT used here — it is owned
  // by AppWindow.cpp (the hidden message window) and gets set during
  // InitializeApplication() → CreateAppWindow(). Using Globals::g_hWindow
  // here would cause it to be overwritten and the GUI would become invisible.
  HWND hGuiWnd = ::CreateWindowW(wc.lpszClassName, windowTitle.c_str(),
                                WS_POPUP | WS_THICKFRAME, 100, 100, 420, 380,
                                NULL, NULL, wc.hInstance, NULL);
  if (!hGuiWnd) {
    MessageBoxA(nullptr, "Failed to create GUI window.",
                "Initialization Error", MB_OK | MB_ICONERROR);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 1;
  }

  EnableBorderlessResize(hGuiWnd);

  // Initialize Direct3D and ImGui
  if (!Gui::GuiManager::GetInstance().Initialize(hGuiWnd)) {
    MessageBoxA(nullptr, "Failed to initialize Direct3D/ImGui.",
                "Initialization Error", MB_OK | MB_ICONERROR);
    ::DestroyWindow(hGuiWnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 1;
  }

  // Initialize backend services (input hook, spam thread, etc.)
  // NOTE: This call sets Globals::g_hWindow to the hidden message window.
  if (!InitializeApplication(hInstance)) {
    Logger::GetInstance().Log("Application Initialization Failed!");
    MessageBoxA(
        NULL,
        "StrafeHelper failed to initialize. See console or log for details.",
        "Initialization Error", MB_OK | MB_ICONERROR);
    Gui::GuiManager::GetInstance().Shutdown();
    ::DestroyWindow(hGuiWnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 1;
  }

  ::ShowWindow(hGuiWnd, SW_SHOWDEFAULT);
  ::UpdateWindow(hGuiWnd);

  Logger::GetInstance().Log("StrafeHelper running. UI Active.");

  // Main loop
  bool done = false;
  while (!done) {
    MSG msg;
    while (::PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE)) {
      ::TranslateMessage(&msg);
      ::DispatchMessage(&msg);
      if (msg.message == WM_QUIT)
        done = true;
    }
    if (done)
      break;

    // Render GUI frame
    Gui::GuiManager::GetInstance().Render();
  }

  Logger::GetInstance().Log("Exiting StrafeHelper...");

  // Cleanup
  CleanupApplication();
  Gui::GuiManager::GetInstance().Shutdown();
  ::DestroyWindow(hGuiWnd);
  ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

  return 0;
}

int main() {
  return WinMain(GetModuleHandle(nullptr), nullptr, GetCommandLineA(),
                 SW_SHOWDEFAULT);
}
