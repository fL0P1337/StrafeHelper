// AppWindow.cpp
#include "AppWindow.h"
#include "Application.h"
#include "Config.h"
#include "Globals.h"
#include "Logger.h"
#include "Utils.h"
#include <tchar.h>
#include <vector>
#include <windows.h>

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
  case WM_CREATE:
    break;

  case WM_INPUT: {
    UINT dwSize = 0;
    GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize,
                    sizeof(RAWINPUTHEADER));
    if (dwSize > 0) {
      std::vector<BYTE> lpb(dwSize);
      if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb.data(), &dwSize,
                          sizeof(RAWINPUTHEADER)) == dwSize) {
        RAWINPUT *raw = (RAWINPUT *)lpb.data();
        if (raw->header.dwType == RIM_TYPEMOUSE) {
          if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_4_DOWN) {
            HandleSideMouseButton(VK_XBUTTON1, true);
          }
          if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_4_UP) {
            HandleSideMouseButton(VK_XBUTTON1, false);
          }
          if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_5_DOWN) {
            HandleSideMouseButton(VK_XBUTTON2, true);
          }
          if (raw->data.mouse.usButtonFlags & RI_MOUSE_BUTTON_5_UP) {
            HandleSideMouseButton(VK_XBUTTON2, false);
          }
        }
      }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
  }

  case Globals::WM_DEFERRED_CONFIG_SAVE:
    Config::SaveConfig();
    return 0;

  case Globals::WM_BACKEND_FAILED:
    Logger::GetInstance().Log(
        "Interception backend became unhealthy; falling back to WinHook.");
    (void)SwitchBackend(Config::InputBackendKind::KbdHook);
    return 0;
  case WM_CLOSE:
    // Hidden message window closing — just destroy, do NOT post quit.
    // The GUI window owns the WM_QUIT lifecycle.
    Logger::GetInstance().Log("Hidden window WM_CLOSE received.");
    DestroyWindow(hwnd);
    break;

  case WM_DESTROY:
    Logger::GetInstance().Log("Hidden window WM_DESTROY received.");
    // Do NOT PostQuitMessage here — that would kill the main GUI message loop
    // when the hidden window is torn down before the GUI window.
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
                              LR_SHARED);
  wc.hIconSm = (HICON)LoadImage(
      hInstance, IDI_APPLICATION, IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
      GetSystemMetrics(SM_CYSMICON), LR_SHARED);
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

  if (!RegisterClassEx(&wc)) {
    LogError("RegisterClassEx failed");
    return false;
  }

  Globals::g_hWindow = CreateWindowEx(
      WS_EX_TOOLWINDOW, Config::WINDOW_CLASS_NAME, Config::WINDOW_TITLE,
      WS_POPUP, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);

  if (!Globals::g_hWindow) {
    LogError("CreateWindowEx failed");
    UnregisterClass(Config::WINDOW_CLASS_NAME, hInstance);
    return false;
  }

  // Register raw input for mouse (side buttons X1/X2)
  RAWINPUTDEVICE rid[1];
  rid[0].usUsagePage = 0x01; // HID_USAGE_PAGE_GENERIC
  rid[0].usUsage = 0x02;     // HID_USAGE_GENERIC_MOUSE
  rid[0].dwFlags = RIDEV_INPUTSINK;
  rid[0].hwndTarget = Globals::g_hWindow;
  if (!RegisterRawInputDevices(rid, 1, sizeof(rid[0]))) {
    // Non-fatal: side mouse buttons won't work, but keyboard features are fine.
    LogError("RegisterRawInputDevices failed — mouse side buttons disabled");
  }

  Logger::GetInstance().Log("Hidden application window created.");
  return true;
}

void DestroyAppWindow() {
  if (Globals::g_hWindow) {
    DestroyWindow(Globals::g_hWindow);
    Globals::g_hWindow = NULL;
  }
}