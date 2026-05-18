// AppWindow.cpp
#include "AppWindow.h"
#include "Application.h"
#include "Config.h"
#include "Globals.h"
#include "Utils.h"
#include <iostream>
#include <tchar.h>
#include <windows.h> // <-- Add


LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_CREATE:
    break;

    break;

  case WM_CLOSE:
    std::cout << "WM_CLOSE received, initiating shutdown." << std::endl;
    DestroyWindow(hwnd);
    break;

  case WM_DESTROY:
    std::cout << "WM_DESTROY received. Cleaning up application..." << std::endl;
    CleanupApplication();
    PostQuitMessage(0);
    break;

  default:
    return DefWindowProc(hwnd, msg, wParam, lParam);
  }
  return 0;
}

bool CreateAppWindow(HINSTANCE hInstance) {
  WNDCLASSEX wc = {0};
  wc.cbSize = sizeof(WNDCLASSEX);
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInstance;
  wc.lpszClassName = Config::WINDOW_CLASS_NAME;
  wc.hIcon = (HICON)LoadImage(hInstance, IDI_APPLICATION, IMAGE_ICON, 0, 0,
                              LR_SHARED); // Use hInstance here
  wc.hIconSm = (HICON)LoadImage(
      hInstance, IDI_APPLICATION, IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
      GetSystemMetrics(SM_CYSMICON), LR_SHARED); // Use hInstance here
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

  if (!RegisterClassEx(&wc)) {
    LogError("RegisterClassEx failed");
    return false;
  }

  // Use Globals:: prefix
  Globals::g_hWindow = CreateWindowEx(
      WS_EX_TOOLWINDOW, Config::WINDOW_CLASS_NAME, Config::WINDOW_TITLE,
      WS_POPUP, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);

  if (!Globals::g_hWindow) { // Globals::
    LogError("CreateWindowEx failed");
    UnregisterClass(Config::WINDOW_CLASS_NAME, hInstance);
    return false;
  }
  std::cout << "Hidden application window created." << std::endl;
  return true;
}

void DestroyAppWindow() {
  if (Globals::g_hWindow) {            // Globals::
    DestroyWindow(Globals::g_hWindow); // Globals::
  }
}