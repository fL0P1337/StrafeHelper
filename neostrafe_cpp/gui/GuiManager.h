// gui/GuiManager.h
#pragma once

#include <d3d11.h>
#include <string>
#include <windows.h>

namespace Gui {

class GuiManager {
public:
  static GuiManager &GetInstance() {
    static GuiManager instance;
    return instance;
  }

  bool Initialize(HWND hwnd);
  void Shutdown();

  // Call this every frame in the main loop
  void Render();

  // Used by WndProc to forward inputs to ImGui
  LRESULT WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
  GuiManager() = default;
  ~GuiManager() = default;
  GuiManager(const GuiManager &) = delete;
  GuiManager &operator=(const GuiManager &) = delete;

  bool CreateDeviceD3D(HWND hWnd);
  void CleanupDeviceD3D();
  void CreateRenderTarget();
  void CleanupRenderTarget();

  // Panel rendering functions
  void RenderMainMenuBar();
  void RenderConfigPanel();
  void RenderConsolePanel();
  void RenderStateMonitor();
  void ApplyDarkTheme();

  // DX11 State
  ID3D11Device *g_pd3dDevice = nullptr;
  ID3D11DeviceContext *g_pd3dDeviceContext = nullptr;
  IDXGISwapChain *g_pSwapChain = nullptr;
  ID3D11RenderTargetView *g_mainRenderTargetView = nullptr;
  HWND m_hwnd = nullptr;

  bool m_showConsole = true;
  bool m_showStateMonitor = true;

  // Rebind state
  bool m_isRebinding = false;
};

} // namespace Gui
