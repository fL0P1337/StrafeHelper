// gui/GuiManager.h
#pragma once

#include "../imgui/imgui.h"
#include <d3d11.h>
#include <string>
#include <windows.h>

namespace Gui {

enum class TabSelection { CONFIG, STATE, CONSOLE };

class GuiManager {
public:
  static GuiManager &GetInstance() {
    static GuiManager instance;
    return instance;
  }

  bool Initialize(HWND hwnd);
  void Shutdown();

  void Render();

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

  void RenderConfigContent();
  void RenderConsoleContent();
  void RenderStateContent();
  void RenderTitleBar();
  void ApplyDarkTheme();

  ID3D11Device *g_pd3dDevice = nullptr;
  ID3D11DeviceContext *g_pd3dDeviceContext = nullptr;
  IDXGISwapChain *g_pSwapChain = nullptr;
  ID3D11RenderTargetView *g_mainRenderTargetView = nullptr;
  HWND m_hwnd = nullptr;

  bool m_showConsole = true;
  bool m_showStateMonitor = true;

  bool m_isRebinding = false;
  bool m_isRebindingLootKey = false;
  bool m_isRebindingJumpKey = false;

  ImFont *m_fontMedium = nullptr;
  ImFont *m_fontSemiBold = nullptr;
  ImFont *m_fontLogo = nullptr;

  TabSelection m_currentTab = TabSelection::CONFIG;
};

} // namespace Gui
