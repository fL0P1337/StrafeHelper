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

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ApplyDarkTheme();

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

  if (msg == WM_SIZE && wParam != SIZE_MINIMIZED && g_pd3dDevice) {
    CleanupRenderTarget();
    g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam),
                                DXGI_FORMAT_UNKNOWN, 0);
    CreateRenderTarget();
    return 0;
  }
  return 0;
}

void GuiManager::RenderTitleBar() {
  float titleBarHeight = 24.0f;
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImVec2 size(ImGui::GetWindowWidth(), titleBarHeight);

  ImDrawList *drawList = ImGui::GetWindowDrawList();
  drawList->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                          IM_COL32(25, 35, 55, 255));

  ImGui::SetCursorPosY(3);
  ImGui::SetCursorPosX(8);
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 0.9f));
  ImGui::Text("StrafeHelper");
  ImGui::PopStyleColor();

  ImGui::SameLine(ImGui::GetWindowWidth() - 120);

  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.3f, 0.5f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4(0.15f, 0.25f, 0.4f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));

  if (ImGui::SmallButton("File")) {
    ImGui::OpenPopup("FileMenu");
  }
  ImGui::SameLine(0, 2);
  if (ImGui::SmallButton("View")) {
    ImGui::OpenPopup("ViewMenu");
  }

  ImGui::PopStyleVar();
  ImGui::PopStyleColor(3);

  if (ImGui::BeginPopup("FileMenu")) {
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
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopup("ViewMenu")) {
    ImGui::Checkbox("Console", &m_showConsole);
    ImGui::Checkbox("State Monitor", &m_showStateMonitor);
    ImGui::EndPopup();
  }

  ImGui::SetCursorPosY(titleBarHeight);
  ImGui::Dummy(ImVec2(0, 0));
}

void GuiManager::Render() {
  if (!g_pd3dDevice || !g_mainRenderTargetView)
    return;

  ImGui_ImplDX11_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);

  ImGuiWindowFlags windowFlags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));

  ImGui::Begin("MainWindow", nullptr, windowFlags);
  {
    ImGui::PopStyleVar(3);

    RenderTitleBar();

    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    float consoleHeight = m_showConsole ? 100.0f : 0.0f;
    float topRowHeight = contentSize.y - consoleHeight - 4;

    ImGui::BeginChild("TopRow", ImVec2(contentSize.x, topRowHeight), false,
                      ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);
    {
      ImGui::Columns(2, "TopColumns", false);

      float configWidth =
          m_showStateMonitor ? contentSize.x * 0.58f : contentSize.x;
      ImGui::SetColumnWidth(0, configWidth);

      ImGui::BeginChild("ConfigSection", ImVec2(-1, -1), true,
                        ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoScrollWithMouse);
      RenderConfigContent();
      ImGui::EndChild();

      if (m_showStateMonitor) {
        ImGui::NextColumn();
        ImGui::BeginChild("StateSection", ImVec2(-1, -1), true,
                          ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse);
        RenderStateContent();
        ImGui::EndChild();
      }

      ImGui::Columns(1);
    }
    ImGui::EndChild();

    if (m_showConsole && consoleHeight > 0) {
      ImGui::BeginChild("ConsoleSection", ImVec2(contentSize.x, consoleHeight),
                        true);
      RenderConsoleContent();
      ImGui::EndChild();
    }
  }
  ImGui::End();

  ImGui::Render();
  const float clear_color_with_alpha[4] = {0.10f, 0.11f, 0.12f, 1.00f};
  g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
  g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView,
                                             clear_color_with_alpha);
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

  g_pSwapChain->Present(1, 0);
}

void GuiManager::RenderConfigContent() {
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(4, 4));

  ImGui::Text("Features");
  ImGui::SameLine(ImGui::GetWindowWidth() - 20);
  ImGui::SetNextItemWidth(-1);

  bool useSpam = Config::EnableSpam.load();
  if (ImGui::Checkbox("##spam", &useSpam)) {
    Config::EnableSpam.store(useSpam);
    Config::SaveConfig();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Enable Macro Spam");

  ImGui::SameLine();
  bool useSnapTap = Config::EnableSnapTap.load();
  if (ImGui::Checkbox("##snaptap", &useSnapTap)) {
    Config::EnableSnapTap.store(useSnapTap);
    Config::SaveConfig();
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Enable Snap-Tap (SOCD)");

  ImGui::Spacing();

  ImGui::Text("Delay");
  ImGui::SameLine(80);
  ImGui::SetNextItemWidth(-8);
  int delay = Config::SpamDelayMs.load();
  if (ImGui::SliderInt("##delay", &delay, 1, 100, "%dms")) {
    Config::SpamDelayMs.store(delay);
  }

  ImGui::Text("Duration");
  ImGui::SameLine(80);
  ImGui::SetNextItemWidth(-8);
  int duration = Config::SpamKeyDownDurationMs.load();
  if (ImGui::SliderInt("##duration", &duration, 0, 50, "%dms")) {
    Config::SpamKeyDownDurationMs.store(duration);
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  ImGui::Text("Trigger");
  ImGui::SameLine(80);
  int currentTrigger = Config::KeySpamTrigger.load();
  if (m_isRebinding) {
    ImGui::Button("...", ImVec2(50, 0));
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
    std::string btnText = std::string(1, (char)currentTrigger);
    if (ImGui::Button(btnText.c_str(), ImVec2(50, 0))) {
      m_isRebinding = true;
    }
  }

  ImGui::PopStyleVar(3);
}

void GuiManager::RenderConsoleContent() {
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));

  ImGui::Text("Console");
  ImGui::SameLine(ImGui::GetWindowWidth() - 45);
  if (ImGui::SmallButton("Clear")) {
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
  ImGui::PopStyleVar();
}

void GuiManager::RenderStateContent() {
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 3));

  ImGui::Text("State");
  ImGui::Separator();
  ImGui::Spacing();

  ImGui::Text("Hook: %s", Globals::g_hHookThread != NULL ? "OK" : "--");
  ImGui::Spacing();

  ImGui::Text("WASD:");
  for (int k : {'W', 'A', 'S', 'D'}) {
    bool down = Globals::g_KeyInfo[k].physicalKeyDown.load();
    ImGui::SameLine();
    ImGui::TextColored(down ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                            : ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                       "%c", k);
  }

  ImGui::Spacing();
  ImGui::Text("Spam: %s", Globals::g_isCSpamActive.load() ? "ON" : "OFF");

  ImGui::PopStyleVar();
}

void GuiManager::ApplyDarkTheme() {
  ImGui::StyleColorsDark();
  ImGuiStyle &style = ImGui::GetStyle();
  style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
  style.Colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
  style.Colors[ImGuiCol_Border] = ImVec4(0.20f, 0.20f, 0.25f, 0.60f);
  style.Colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.18f, 0.26f, 1.00f);
  style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.24f, 0.36f, 1.00f);
  style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.30f, 0.46f, 1.00f);
  style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.16f, 0.29f, 0.48f, 1.00f);
  style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.14f, 0.22f, 1.00f);
  style.Colors[ImGuiCol_CheckMark] = ImVec4(0.4f, 0.8f, 0.4f, 1.0f);
  style.WindowRounding = 0.0f;
  style.ChildRounding = 3.0f;
  style.FrameRounding = 3.0f;
  style.WindowBorderSize = 0.0f;
  style.ChildBorderSize = 1.0f;
  style.FrameBorderSize = 0.0f;
  style.ItemSpacing = ImVec2(8, 4);
  style.ItemInnerSpacing = ImVec2(4, 4);
  style.WindowPadding = ImVec2(4, 4);
  style.FramePadding = ImVec2(4, 2);
  style.IndentSpacing = 12.0f;
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
