// main.cpp
#define WIN32_LEAN_AND_MEAN
#include "Application.h"
#include "Config.h"
#include "Logger.h"
#include "gui/GuiManager.h"
#include "imgui/imgui.h"
#include <string>
#include <windows.h>
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

HWND g_hwnd = NULL;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

static void EnableBorderlessResize(HWND hwnd) {
  MARGINS margins = { -1, -1, -1, -1 };
  DwmExtendFrameIntoClientArea(hwnd, &margins);
  SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
               SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                              LPARAM lParam) {
  if (msg == WM_NCHITTEST) {
    LRESULT hit = DefWindowProc(hwnd, msg, wParam, lParam);
    if (hit == HTCLIENT) {
      POINT pt = {LOWORD(lParam), HIWORD(lParam)};
      ScreenToClient(hwnd, &pt);
      RECT rc;
      GetClientRect(hwnd, &rc);

      const int resizeBorder = 6;
      const int dragBorder = 30;

      bool left = pt.x < resizeBorder;
      bool right = pt.x >= rc.right - resizeBorder;
      bool top = pt.y < resizeBorder;
      bool bottom = pt.y >= rc.bottom - resizeBorder;

      if (top && left) return HTTOPLEFT;
      if (top && right) return HTTOPRIGHT;
      if (bottom && left) return HTBOTTOMLEFT;
      if (bottom && right) return HTBOTTOMRIGHT;
      if (left) return HTLEFT;
      if (right) return HTRIGHT;
      if (top) return HTTOP;
      if (bottom) return HTBOTTOM;

      if (pt.y < dragBorder || pt.y > (rc.bottom - dragBorder))
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
  ::RegisterClassExW(&wc);

  std::wstring windowTitle =
      std::wstring(Config::APP_NAME,
                   Config::APP_NAME + strlen(Config::APP_NAME)) +
      L" - Configuration";

  // Create application window (WS_THICKFRAME required for native resize)
  g_hwnd = ::CreateWindowW(wc.lpszClassName, windowTitle.c_str(),
                           WS_POPUP | WS_THICKFRAME, 100,
                           100, 420, 380, NULL, NULL, wc.hInstance, NULL);

  EnableBorderlessResize(g_hwnd);

  // Initialize Direct3D and ImGui
  if (!Gui::GuiManager::GetInstance().Initialize(g_hwnd)) {
    ::DestroyWindow(g_hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 1;
  }

  // Initialize backend services (Hook thread, Spam thread)
  // We pass hInstance to let application setup tray or background hooks
  if (!InitializeApplication(hInstance)) {
    Logger::GetInstance().Log("Application Initialization Failed!");
    MessageBoxA(
        NULL,
        "StrafeHelper failed to initialize. See console or log for details.",
        "Initialization Error", MB_OK | MB_ICONERROR);
    Gui::GuiManager::GetInstance().Shutdown();
    ::DestroyWindow(g_hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 1;
  }

  ::ShowWindow(g_hwnd, SW_SHOWDEFAULT);
  ::UpdateWindow(g_hwnd);

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
  ::DestroyWindow(g_hwnd);
  ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

  return 0;
}

int main() {
  return WinMain(GetModuleHandle(nullptr), nullptr, GetCommandLineA(),
                 SW_SHOWDEFAULT);
}
