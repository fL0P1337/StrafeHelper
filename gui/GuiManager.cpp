// gui/GuiManager.cpp
#include "GuiManager.h"
#include "../Config.h"
#include "../Globals.h"
#include "../Logger.h"
#include "../catrine/byte.h"
#include "../catrine/elements.h"
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

  // Load Fonts
  ImFontConfig font_config;
  font_config.PixelSnapH = false;
  font_config.OversampleH = 5;
  font_config.OversampleV = 5;
  font_config.RasterizerMultiply = 1.2f;

  static const ImWchar ranges[] = {
      0x0020, 0x00FF, // Basic Latin + Latin Supplement
      0x0400, 0x052F, // Cyrillic + Cyrillic Supplement
      0x2DE0, 0x2DFF, // Cyrillic Extended-A
      0xA640, 0xA69F, // Cyrillic Extended-B
      0xE000, 0xE226, // icons
      0,
  };

  font_config.GlyphRanges = ranges;

  m_fontMedium = io.Fonts->AddFontFromMemoryTTF(
      (void *)InterMedium, sizeof(InterMedium), 15.0f, &font_config, ranges);
  m_fontSemiBold = io.Fonts->AddFontFromMemoryTTF((void *)InterSemiBold,
                                                  sizeof(InterSemiBold), 17.0f,
                                                  &font_config, ranges);
  m_fontLogo = io.Fonts->AddFontFromMemoryTTF(
      (void *)catrine_logo, sizeof(catrine_logo), 17.0f, &font_config, ranges);

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

void GuiManager::Render() {
  if (!g_pd3dDevice || !g_mainRenderTargetView)
    return;

  ImGui_ImplDX11_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);

  ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus;

  // Remove all padding, rounding, and borders to eliminate visual artifacts
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.08f, 0.10f, 1.00f));

  ImGui::Begin("StrafeHelper", nullptr, windowFlags);
  {
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();

    auto draw = ImGui::GetWindowDrawList();
    auto pos = ImGui::GetWindowPos();
    auto size = ImGui::GetWindowSize();

    // Title bar area only — no full-window manual background needed
    // (ImGui's WindowBg handles the main background via ClearRenderTargetView)
    draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + 51),
                        ImColor(24, 24, 24), 0.0f);

    // Subtle gradient accent on left side of title bar
    ImGui::PushClipRect(pos, ImVec2(pos.x + 55, pos.y + 51), true);
    draw->AddRectFilledMultiColor(
        pos, ImVec2(pos.x + 55, pos.y + 51), ImColor(1.0f, 1.0f, 1.0f, 0.00f),
        ImColor(1.0f, 1.0f, 1.0f, 0.05f), ImColor(1.0f, 1.0f, 1.0f, 0.00f),
        ImColor(1.0f, 1.0f, 1.0f, 0.05f));
    ImGui::PopClipRect();

    ImGui::PushFont(m_fontLogo);
    draw->AddText(ImVec2(pos.x + 25, pos.y + 17), ImColor(192, 203, 229), "A");
    ImGui::PopFont();

    ImGui::PushFont(m_fontSemiBold);
    draw->AddText(ImVec2(pos.x + 49, pos.y + 18), ImColor(192, 203, 229),
                  "StrafeHelper");
    ImGui::PopFont();

    ImGui::SetCursorPos({160, 19});
    ImGui::BeginGroup();
    {
      // Calculate responsive tab widths based on available window space
      const float tabAreaStart = 160.0f;
      const float tabAreaEnd = size.x - 15.0f; // Right padding
      const float availableWidth = tabAreaEnd - tabAreaStart;
      const int numTabs = 3;
      const float tabSpacing = 4.0f; // Spacing between tabs
      const float totalSpacing = tabSpacing * (numTabs - 1);
      const float tabWidth = (availableWidth - totalSpacing) / numTabs;

      if (elements::tab("Config", m_currentTab == TabSelection::CONFIG,
                        tabWidth))
        m_currentTab = TabSelection::CONFIG;
      ImGui::SameLine(0, tabSpacing);
      if (elements::tab("Monitor", m_currentTab == TabSelection::STATE,
                        tabWidth))
        m_currentTab = TabSelection::STATE;
      ImGui::SameLine(0, tabSpacing);
      if (elements::tab("Console", m_currentTab == TabSelection::CONSOLE,
                        tabWidth))
        m_currentTab = TabSelection::CONSOLE;
    }
    ImGui::EndGroup();

    ImGui::PushFont(m_fontMedium);

    // Content area below title bar, with margins. Transparent child
    // constrains the content region width without drawing a visible panel.
    ImGui::SetCursorPos({15, 65});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("MainContent", ImVec2(size.x - 30, size.y - 80), false,
                      ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoScrollWithMouse);

    if (m_currentTab == TabSelection::CONFIG) {
      RenderConfigContent();
    } else if (m_currentTab == TabSelection::STATE) {
      RenderStateContent();
    } else if (m_currentTab == TabSelection::CONSOLE) {
      RenderConsoleContent();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopFont();
  }
  ImGui::End();

  ImGui::Render();
  // Use fully opaque background color matching the theme
  const float clear_color_with_alpha[4] = {0.08f, 0.08f, 0.10f, 1.00f};
  g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
  g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView,
                                             clear_color_with_alpha);
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

  g_pSwapChain->Present(1, 0);
}

void GuiManager::RenderConfigContent() {
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

  // Slider width = 45% of available content region (bounded by child window)
  ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);

  // Helper: resolve VK code to a readable key name
  auto VkToName = [](int vk, char *buf, int bufSize) {
    UINT sc = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    if (sc != 0) {
      GetKeyNameTextA((sc << 16), buf, bufSize);
    } else {
      sprintf_s(buf, bufSize, "VK %d", vk);
    }
  };

  // Helper: generic key rebind button
  auto RebindButton = [&](const char *label, bool &rebinding,
                          std::atomic<int> &target) {
    if (rebinding) {
      ImGui::Button("Press any key...", ImVec2(150, 0));
      ImGui::SameLine();
      ImGui::TextDisabled("%s", label);
      for (int i = 1; i < 256; i++) {
        if (i == VK_LBUTTON || i == VK_RBUTTON || i == VK_ESCAPE)
          continue;
        if (GetAsyncKeyState(i) & 0x8000) {
          target.store(i);
          Config::SaveConfig();
          Logger::GetInstance().Log(std::string("Rebound ") + label +
                                    " to VK " + std::to_string(i));
          rebinding = false;
          break;
        }
      }
    } else {
      char keyName[64] = "Unknown Key";
      VkToName(target.load(), keyName, sizeof(keyName));
      if (ImGui::Button(keyName, ImVec2(150, 0))) {
        rebinding = true;
      }
      ImGui::SameLine();
      ImGui::TextDisabled("%s", label);
    }
  };

  ImGui::TextDisabled("Features");
  ImGui::Separator();

  bool useSpam = Config::EnableSpam.load();
  if (ImGui::Checkbox("Enable Macro Spam", &useSpam)) {
    Config::EnableSpam.store(useSpam);
    Config::SaveConfig();
  }

  if (useSpam) {
    int delay = Config::SpamDelayMs.load();
    if (ImGui::SliderInt("Spam Delay", &delay, 1, 100, "%dms")) {
      Config::SpamDelayMs.store(delay);
      Config::SaveConfig();
    }

    int duration = Config::SpamKeyDownDurationMs.load();
    if (ImGui::SliderInt("Hold Duration", &duration, 0, 50, "%dms")) {
      Config::SpamKeyDownDurationMs.store(duration);
      Config::SaveConfig();
    }

    RebindButton("Trigger Key", m_isRebinding, Config::KeySpamTrigger);
  }

  bool useSnapTap = Config::EnableSnapTap.load();
  if (ImGui::Checkbox("Enable SnapTap (SOCD)", &useSnapTap)) {
    Config::EnableSnapTap.store(useSnapTap);
    Config::SaveConfig();
  }

  ImGui::Spacing();
  ImGui::TextDisabled("Turbo Functions");
  ImGui::Separator();

  bool useTurboLoot = Config::EnableTurboLoot.load();
  if (ImGui::Checkbox("Enable Turbo Loot", &useTurboLoot)) {
    Config::EnableTurboLoot.store(useTurboLoot);
    Config::SaveConfig();
  }

  if (useTurboLoot) {
    int lootDelay = Config::TurboLootDelayMs.load();
    if (ImGui::SliderInt("Loot Spam Delay", &lootDelay, 1, 100, "%dms")) {
      Config::TurboLootDelayMs.store(lootDelay);
      Config::SaveConfig();
    }

    int lootDuration = Config::TurboLootDurationMs.load();
    if (ImGui::SliderInt("Loot Hold Duration", &lootDuration, 0, 50, "%dms")) {
      Config::TurboLootDurationMs.store(lootDuration);
      Config::SaveConfig();
    }

    RebindButton("Loot Key", m_isRebindingLootKey, Config::TurboLootKey);
  }

  bool useTurboJump = Config::EnableTurboJump.load();
  if (ImGui::Checkbox("Enable Turbo Jump", &useTurboJump)) {
    Config::EnableTurboJump.store(useTurboJump);
    Config::SaveConfig();
  }

  if (useTurboJump) {
    int jumpDelay = Config::TurboJumpDelayMs.load();
    if (ImGui::SliderInt("Jump Spam Delay", &jumpDelay, 1, 100, "%dms")) {
      Config::TurboJumpDelayMs.store(jumpDelay);
      Config::SaveConfig();
    }

    int jumpDuration = Config::TurboJumpDurationMs.load();
    if (ImGui::SliderInt("Jump Hold Duration", &jumpDuration, 0, 50, "%dms")) {
      Config::TurboJumpDurationMs.store(jumpDuration);
      Config::SaveConfig();
    }

    RebindButton("Jump Key", m_isRebindingJumpKey, Config::TurboJumpKey);
  }

  ImGui::PopItemWidth();
  ImGui::PopStyleVar(2);
}

void GuiManager::RenderConsoleContent() {
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

  ImGui::TextDisabled("Application Logs");
  ImGui::SameLine(ImGui::GetWindowWidth() - 55);
  if (ImGui::SmallButton("Clear")) {
    Logger::GetInstance().Clear();
  }
  ImGui::Separator();

  auto logs = Logger::GetInstance().GetRecentLogs();
  std::string fullLog;
  for (const auto &log : logs) {
    fullLog += log + "\n";
  }

  ImVec2 avail = ImGui::GetContentRegionAvail();
  ImGui::InputTextMultiline("##ConsoleOut", (char *)fullLog.c_str(),
                            fullLog.size() + 1, avail,
                            ImGuiInputTextFlags_ReadOnly);

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

  // Unified dark background - consistent across all elements
  const ImVec4 bgColor = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);

  style.Colors[ImGuiCol_WindowBg] = bgColor;
  style.Colors[ImGuiCol_ChildBg] =
      ImVec4(0.0f, 0.0f, 0.0f, 0.0f); // transparent — inherits parent
  style.Colors[ImGuiCol_PopupBg] = ImVec4(0.08f, 0.08f, 0.10f, 0.95f);
  // Set border to match background to eliminate any white lines
  style.Colors[ImGuiCol_Border] = ImVec4(0.08f, 0.08f, 0.10f, 0.00f);
  style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  style.Colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.18f, 0.26f, 1.00f);
  style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.24f, 0.36f, 1.00f);
  style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.30f, 0.46f, 1.00f);
  style.Colors[ImGuiCol_TitleBg] = bgColor;
  style.Colors[ImGuiCol_TitleBgActive] = bgColor;
  style.Colors[ImGuiCol_TitleBgCollapsed] = bgColor;
  style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.10f, 0.14f, 0.22f, 1.00f);
  style.Colors[ImGuiCol_CheckMark] = ImVec4(0.4f, 0.8f, 0.4f, 1.0f);
  style.Colors[ImGuiCol_Separator] = ImVec4(0.15f, 0.15f, 0.18f, 1.00f);
  style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.20f, 0.20f, 0.25f, 1.00f);
  style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);

  // Remove all rounding and borders for cleaner look
  style.WindowRounding = 0.0f;
  style.ChildRounding = 0.0f;
  style.FrameRounding = 3.0f;
  style.WindowBorderSize = 0.0f;
  style.ChildBorderSize = 0.0f;
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
