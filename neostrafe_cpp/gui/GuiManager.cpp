// gui/GuiManager.cpp
#include "GuiManager.h"
#include "../Config.h"
#include "../Globals.h"
#include "../Logger.h"
#include "../imgui/backends/imgui_impl_dx11.h"
#include "../imgui/backends/imgui_impl_win32.h"
#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"
#include <iostream>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd,
                                                             UINT msg,
                                                             WPARAM wParam,
                                                             LPARAM lParam);

namespace Gui {

bool GuiManager::Initialize(HWND hwnd) {
  m_hwnd = hwnd;
  if (!CreateDeviceD3D(hwnd)) {
    CleanupDeviceD3D();
    Logger::GetInstance().Log("Failed to create D3D Device for GUI.");
    return false;
  }

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |=
      ImGuiConfigFlags_NavEnableKeyboard;           // Enable Keyboard Controls
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking

  ApplyDarkTheme();

  // Setup Platform/Renderer backends
  ImGui_ImplWin32_Init(hwnd);
  ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

  Logger::GetInstance().Log("GuiManager initialized successfully.");
  return true;
}

void GuiManager::Shutdown() {
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplWin32_Shutdown();
  ImGui::DestroyContext();

  CleanupDeviceD3D();
  Logger::GetInstance().Log("GuiManager shutdown completed.");
}

LRESULT GuiManager::WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam,
                                   LPARAM lParam) {
  if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam))
    return true;

  // Handle resizing
  if (msg == WM_SIZE && wParam != SIZE_MINIMIZED && g_pd3dDevice) {
    CleanupRenderTarget();
    g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam),
                                DXGI_FORMAT_UNKNOWN, 0);
    CreateRenderTarget();
    return 0;
  }
  return 0;
}

void GuiManager::Render() {
  if (!g_pd3dDevice || !g_mainRenderTargetView)
    return;

  // Start the Dear ImGui frame
  ImGui_ImplDX11_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  // Enable dockspace over the entire window
  ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(
      0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);

  static bool first_time = true;
  if (first_time) {
    first_time = false;

    // Clear existing layout
    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

    ImGuiID dock_main_id = dockspace_id;
    ImGuiID dock_id_bottom = ImGui::DockBuilderSplitNode(
        dock_main_id, ImGuiDir_Down, 0.25f, NULL, &dock_main_id);
    ImGuiID dock_id_right = ImGui::DockBuilderSplitNode(
        dock_main_id, ImGuiDir_Right, 0.35f, NULL, &dock_main_id);
    ImGuiID dock_id_left = dock_main_id;

    ImGui::DockBuilderDockWindow("Configuration", dock_id_left);
    ImGui::DockBuilderDockWindow("State Monitor", dock_id_right);
    ImGui::DockBuilderDockWindow("Console", dock_id_bottom);
    ImGui::DockBuilderFinish(dockspace_id);
  }

  RenderMainMenuBar();
  RenderConfigPanel();
  if (m_showConsole)
    RenderConsolePanel();
  if (m_showStateMonitor)
    RenderStateMonitor();

  // Rendering
  ImGui::Render();
  const float clear_color_with_alpha[4] = {0.10f, 0.11f, 0.12f, 1.00f};
  g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
  g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView,
                                             clear_color_with_alpha);
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

  // Update and Render additional Platform Windows
  ImGuiIO &io = ImGui::GetIO();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
  }

  g_pSwapChain->Present(1, 0); // Present with vsync
}

void GuiManager::RenderMainMenuBar() {
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Save Config")) {
        Config::SaveConfig();
        Logger::GetInstance().Log("Config saved manually.");
      }
      if (ImGui::MenuItem("Load Config")) {
        Config::LoadConfig();
        Logger::GetInstance().Log("Config loaded manually.");
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Exit")) {
        PostMessage(m_hwnd, WM_CLOSE, 0, 0);
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
      ImGui::MenuItem("Console", NULL, &m_showConsole);
      ImGui::MenuItem("State Monitor", NULL, &m_showStateMonitor);
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }
}

void GuiManager::RenderConfigPanel() {
  ImGui::Begin("Configuration", NULL,
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

  bool enableHook =
      Config::EnableSpam.load() ||
      Config::EnableSnapTap
          .load(); // Proxy to tell if hook should be active globally - though
                   // we manage features separately now

  ImGui::Text("Features");
  ImGui::Separator();

  bool useSpam = Config::EnableSpam.load();
  if (ImGui::Checkbox("Enable Macro Spam logic", &useSpam)) {
    Config::EnableSpam.store(useSpam);
    Config::SaveConfig();
  }

  bool useSnapTap = Config::EnableSnapTap.load();
  if (ImGui::Checkbox("Enable Snap-Tap (SOCD)", &useSnapTap)) {
    Config::EnableSnapTap.store(useSnapTap);
    Config::SaveConfig();
    // Call OnSnapTapToggled logic here ideally, but for now we rely on the
    // backend catching state updates
  }

  ImGui::Spacing();
  ImGui::Text("Timings");
  ImGui::Separator();

  int delay = Config::SpamDelayMs.load();
  if (ImGui::SliderInt("Spam Delay (ms)", &delay, 1, 100)) {
    Config::SpamDelayMs.store(delay);
  }

  int duration = Config::SpamKeyDownDurationMs.load();
  if (ImGui::SliderInt("Key-Down Duration (ms)", &duration, 0, 50)) {
    Config::SpamKeyDownDurationMs.store(duration);
  }

  ImGui::Spacing();
  ImGui::Text("Bindings");
  ImGui::Separator();

  int currentTrigger = Config::KeySpamTrigger.load();
  if (m_isRebinding) {
    ImGui::Button("Press any key...");
    // Detect key logic
    for (int i = 8; i < 256; i++) {
      if (ImGui::IsKeyPressed((ImGuiKey)i) || (GetAsyncKeyState(i) & 0x8000)) {
        if (i != 'W' && i != 'A' && i != 'S' && i != 'D' && i != VK_ESCAPE) {
          Config::KeySpamTrigger.store(i);
          Config::SaveConfig();
          Logger::GetInstance().Log("Rebound trigger to VK " +
                                    std::to_string(i));
        }
        m_isRebinding = false;
        break;
      }
    }
  } else {
    std::string btnText = "Spam Trigger: [ " +
                          std::string(1, (char)currentTrigger) +
                          " ] (Click to rebind)";
    if (ImGui::Button(btnText.c_str())) {
      m_isRebinding = true;
    }
  }
  ImGui::End();
}

void GuiManager::RenderConsolePanel() {
  ImGui::Begin("Console", NULL,
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
  if (ImGui::Button("Clear")) {
    Logger::GetInstance().Clear();
  }
  ImGui::Separator();

  ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false,
                    ImGuiWindowFlags_HorizontalScrollbar);

  auto logs = Logger::GetInstance().GetRecentLogs();
  for (const auto &log : logs) {
    ImGui::TextUnformatted(log.c_str());
  }

  if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
    ImGui::SetScrollHereY(1.0f);

  ImGui::EndChild();
  ImGui::End();
}

void GuiManager::RenderStateMonitor() {
  ImGui::Begin("State Monitor", NULL,
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
  ImGui::Text("Hook Thread State");
  ImGui::Separator();
  ImGui::Text("Running: %s", Globals::g_hHookThread != NULL ? "YES" : "NO");

  ImGui::Spacing();
  ImGui::Text("Keys Physically Down (WASD)");
  for (int k : {'W', 'A', 'S', 'D'}) {
    ImGui::Text("%c: %s", k,
                Globals::g_KeyInfo[k].physicalKeyDown.load() ? "DOWN" : "UP");
  }

  ImGui::Spacing();
  ImGui::Text("Spam Active Flag: %s",
              Globals::g_isCSpamActive.load() ? "TRUE" : "FALSE");

  ImGui::End();
}

void GuiManager::ApplyDarkTheme() {
  ImGui::StyleColorsDark();
  ImGuiStyle &style = ImGui::GetStyle();
  style.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.06f, 0.06f, 0.94f);
  style.Colors[ImGuiCol_Border] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
  style.Colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.29f, 0.48f, 0.54f);
  style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.29f, 0.48f, 1.00f);
  style.WindowRounding = 4.0f;
}

bool GuiManager::CreateDeviceD3D(HWND hWnd) {
  DXGI_SWAP_CHAIN_DESC sd;
  ZeroMemory(&sd, sizeof(sd));
  sd.BufferCount = 2;
  sd.BufferDesc.Width = 0;
  sd.BufferDesc.Height = 0;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferDesc.RefreshRate.Numerator = 60;
  sd.BufferDesc.RefreshRate.Denominator = 1;
  sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hWnd;
  sd.SampleDesc.Count = 1;
  sd.SampleDesc.Quality = 0;
  sd.Windowed = TRUE;
  sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  UINT createDeviceFlags = 0;

  D3D_FEATURE_LEVEL featureLevel;
  const D3D_FEATURE_LEVEL featureLevelArray[2] = {
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_0,
  };
  if (D3D11CreateDeviceAndSwapChain(
          NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags,
          featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
          &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext) != S_OK)
    return false;

  CreateRenderTarget();
  return true;
}

void GuiManager::CleanupDeviceD3D() {
  CleanupRenderTarget();
  if (g_pSwapChain) {
    g_pSwapChain->Release();
    g_pSwapChain = NULL;
  }
  if (g_pd3dDeviceContext) {
    g_pd3dDeviceContext->Release();
    g_pd3dDeviceContext = NULL;
  }
  if (g_pd3dDevice) {
    g_pd3dDevice->Release();
    g_pd3dDevice = NULL;
  }
}

void GuiManager::CreateRenderTarget() {
  ID3D11Texture2D *pBackBuffer;
  g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
  g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL,
                                       &g_mainRenderTargetView);
  pBackBuffer->Release();
}

void GuiManager::CleanupRenderTarget() {
  if (g_mainRenderTargetView) {
    g_mainRenderTargetView->Release();
    g_mainRenderTargetView = NULL;
  }
}

} // namespace Gui
