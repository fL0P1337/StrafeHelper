// main.cpp
#define WIN32_LEAN_AND_MEAN
#include "Application.h"
#include "Config.h"
#include "Logger.h"
#include "gui/GuiManager.h"
#include "imgui/imgui.h"
#include <string>
#include <windows.h>

// Global window handle for the main GUI
HWND g_hwnd = NULL;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam,
                             LPARAM lParam) {
  if (Gui::GuiManager::GetInstance().WndProcHandler(hwnd, msg, wParam, lParam))
    return true;

  switch (msg) {
  case WM_NCHITTEST: {
    LRESULT hit = DefWindowProc(hwnd, msg, wParam, lParam);
    if (hit == HTCLIENT && ImGui::GetCurrentContext() != nullptr) {
      if (!ImGui::GetIO().WantCaptureMouse) {
        POINT pt = {LOWORD(lParam), HIWORD(lParam)};
        ScreenToClient(hwnd, &pt);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int border = 30; // 30px handles for top/bottom
        if (pt.y < border || pt.y > (rc.bottom - border))
          return HTCAPTION;
      }
    }
    return hit;
  }
  case WM_SYSCOMMAND:
    if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
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

  // Create application window
  g_hwnd = ::CreateWindowW(wc.lpszClassName, windowTitle.c_str(), WS_POPUP, 100,
                           100, 800, 600, NULL, NULL, wc.hInstance, NULL);

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
