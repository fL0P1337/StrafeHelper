// gui/GuiManager.cpp
#include "GuiManager.h"
#include "../Application.h"
#include "../Config.h"
#include "../Globals.h"
#include "../KeybindManager.h"
#include "../Logger.h"
#include "../catrine/byte.h"
#include "../catrine/elements.h"
#include "../imgui/backends/imgui_impl_dx11.h"
#include "../imgui/backends/imgui_impl_win32.h"
#include "../imgui/imgui.h"
#include "../imgui/imgui_internal.h"
#include <algorithm>
#include <cmath>
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

    const float titleBarH = 51.0f;
    ImGui::SetCursorPos({160, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(4, (titleBarH - ImGui::GetTextLineHeight()) * 0.5f));
    ImGui::BeginGroup();
    {
      const float tabAreaStart = 160.0f;
      const float tabAreaEnd = size.x - 15.0f;
      const float availableWidth = tabAreaEnd - tabAreaStart;
      const int numTabs = 3;
      const float tabSpacing = 4.0f;
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
    ImGui::PopStyleVar();

    ImGui::PushFont(m_fontMedium);

    // Content area below title bar, with margins. Transparent child
    // constrains the content region width without drawing a visible panel.
    // Config tab needs scrolling enabled so all controls are reachable.
    ImGui::SetCursorPos({15, 65});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    const bool isConsoleTab = (m_currentTab == TabSelection::CONSOLE);
    ImGuiWindowFlags childFlags =
        isConsoleTab
            ? (ImGuiWindowFlags_NoScrollbar |
               ImGuiWindowFlags_NoScrollWithMouse)
            : ImGuiWindowFlags_None;
    ImGui::BeginChild("MainContent", ImVec2(size.x - 30, size.y - 80), false,
                      childFlags);

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
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));

  // Macro that renders a slider in the reference style:
  //   Label ................ value
  //   [========== bar ==========]
#define FULL_SLIDER_INT(label, var, vmin, vmax, fmt, saveFn)                   \
  do {                                                                         \
    char _vbuf[16];                                                            \
    ImGui::Text("%s", label);                                                  \
    snprintf(_vbuf, sizeof(_vbuf), fmt, (var));                                \
    ImGui::SameLine(ImGui::GetContentRegionAvail().x -                         \
                    ImGui::CalcTextSize(_vbuf).x);                             \
    ImGui::TextDisabled("%s", _vbuf);                                          \
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,                            \
        ImVec2(ImGui::GetStyle().FramePadding.x, 2.0f));                       \
    ImGui::SetNextItemWidth(-FLT_MIN);                                         \
    if (ImGui::SliderInt("##" label, &(var), vmin, vmax, "")) {                \
      saveFn;                                                                  \
    }                                                                          \
    ImGui::PopStyleVar();                                                      \
  } while (false)

  // Helper: resolve VK code to a readable key name
  auto VkToName = [](int vk, char *buf, int bufSize) {
    UINT sc = MapVirtualKeyA(vk, MAPVK_VK_TO_VSC);
    if (sc != 0) {
      GetKeyNameTextA((sc << 16), buf, bufSize);
    } else {
      sprintf_s(buf, bufSize, "VK %d", vk);
    }
  };

  // Returns the label of another feature using the same VK, or nullptr.
  auto FindKeybindConflict = [](int vk, const std::atomic<int> &self)
      -> const char * {
    struct BindEntry {
      const std::atomic<int> *key;
      const std::atomic<bool> *enabled;
      const char *name;
    };
    const BindEntry binds[] = {
        {&Config::KeySpamTrigger, &Config::EnableSpam, "Lurch Trigger"},
        {&Config::TurboLootKey, &Config::EnableTurboLoot, "Turbo Loot"},
        {&Config::TurboJumpKey, &Config::EnableTurboJump, "Turbo Jump"},
        {&Config::SuperglideBind, &Config::EnableSuperglide, "Superglide"},
    };
    for (const auto &b : binds) {
      if (b.key == &self)
        continue;
      if (b.key->load(std::memory_order_relaxed) == vk)
        return b.name;
    }
    return nullptr;
  };

  // Helper: generic key rebind button with conflict warning
  auto RebindButton = [&](const char *label, bool &rebinding,
                          std::atomic<int> &target) {
    if (rebinding) {
      ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.10f, 1.0f), "Press any key...");
      ImGui::SameLine();
      ImGui::TextDisabled("%s", label);
      for (int i = 1; i < 256; i++) {
        if (i == VK_LBUTTON || i == VK_RBUTTON || i == VK_MBUTTON ||
            i == VK_ESCAPE)
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
      if (ImGui::Button(keyName, ImVec2(0, 0))) {
        rebinding = true;
      }
      ImGui::SameLine();
      ImGui::TextDisabled("%s", label);
    }

    const int currentVk = target.load(std::memory_order_relaxed);
    const char *conflict = FindKeybindConflict(currentVk, target);
    if (conflict) {
      char warnBuf[128];
      char conflictKeyName[64] = "?";
      VkToName(currentVk, conflictKeyName, sizeof(conflictKeyName));
      snprintf(warnBuf, sizeof(warnBuf), "Conflict: '%s' is also used by %s",
               conflictKeyName, conflict);
      ImGui::TextColored(ImVec4(0.95f, 0.40f, 0.30f, 1.0f), "%s", warnBuf);
    }
  };

  // Shared child layout for all feature blocks:
  // - keeps checkboxes aligned
  // - keeps child controls consistently indented
  constexpr float kFeatureChildIndent = 16.0f;
  auto BeginFeatureChildren = [&](bool enabled) {
    if (!enabled) {
      return false;
    }
    ImGui::Indent(kFeatureChildIndent);
    ImGui::BeginGroup();
    return true;
  };
  auto EndFeatureChildren = [&]() {
    ImGui::EndGroup();
    ImGui::Unindent(kFeatureChildIndent);
  };
  auto RenderBindModeSelector = [&](const char *id,
                                    std::atomic<Config::KeybindMode> &mode) {
    int bindMode = static_cast<int>(mode.load(std::memory_order_relaxed));
    const char *bindModes[] = {"Hold", "Toggle"};
    float comboWidth = ImGui::CalcTextSize("Toggle").x +
                       ImGui::GetStyle().FramePadding.x * 2.0f +
                       ImGui::GetFrameHeight();
    ImGui::SetNextItemWidth(comboWidth);
    if (ImGui::Combo(id, &bindMode, bindModes, IM_ARRAYSIZE(bindModes))) {
      mode.store(static_cast<Config::KeybindMode>(bindMode),
                 std::memory_order_relaxed);
      Config::SaveConfig();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Bind Mode");
  };

  ImGui::TextDisabled("Features");
  ImGui::Separator();

  bool useSpam = Config::EnableSpam.load();
  if (ImGui::Checkbox("Lurch Strafing", &useSpam)) {
    Config::EnableSpam.store(useSpam);
    Config::SaveConfig();
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Spams movement keys to change air-strafe directions.");

  if (BeginFeatureChildren(useSpam)) {
    RebindButton("Trigger Key", m_isRebinding, Config::KeySpamTrigger);
    RenderBindModeSelector("Bind Mode", Config::KeySpamTriggerMode);

    int delay = Config::SpamDelayMs.load();
    FULL_SLIDER_INT("Spam Delay", delay, 1, 100, "%dms",
                    (Config::SpamDelayMs.store(delay), Config::SaveConfig()));

    int duration = Config::SpamKeyDownDurationMs.load();
    FULL_SLIDER_INT(
        "Hold Duration", duration, 0, 50, "%dms",
        (Config::SpamKeyDownDurationMs.store(duration), Config::SaveConfig()));
    EndFeatureChildren();
  }

  bool useSnapTap = Config::EnableSnapTap.load();
  if (ImGui::Checkbox("SnapTap (SOCD)", &useSnapTap)) {
    Config::EnableSnapTap.store(useSnapTap);
    Config::SaveConfig();
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Prevents simultaneous movement key conflicts (A+D / W+S).");

  ImGui::Spacing();
  ImGui::TextDisabled("Turbo Functions");
  ImGui::Separator();

  bool useTurboLoot = Config::EnableTurboLoot.load();
  if (ImGui::Checkbox("Turbo Loot", &useTurboLoot)) {
    Config::EnableTurboLoot.store(useTurboLoot);
    Config::SaveConfig();
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Spams your loot key, useful for fast-looting.");

  if (BeginFeatureChildren(useTurboLoot)) {
    RebindButton("Loot Key", m_isRebindingLootKey, Config::TurboLootKey);
    RenderBindModeSelector("Bind Mode", Config::TurboLootMode);

    int lootDelay = Config::TurboLootDelayMs.load();
    FULL_SLIDER_INT(
        "Loot Spam Delay", lootDelay, 1, 100, "%dms",
        (Config::TurboLootDelayMs.store(lootDelay), Config::SaveConfig()));

    int lootDuration = Config::TurboLootDurationMs.load();
    FULL_SLIDER_INT("Loot Hold Duration", lootDuration, 0, 50, "%dms",
                    (Config::TurboLootDurationMs.store(lootDuration),
                     Config::SaveConfig()));
    EndFeatureChildren();
  }

  bool useTurboJump = Config::EnableTurboJump.load();
  if (ImGui::Checkbox("Turbo Jump", &useTurboJump)) {
    Config::EnableTurboJump.store(useTurboJump);
    Config::SaveConfig();
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Spams the jump key, useful for bunnyhopping.");

  if (BeginFeatureChildren(useTurboJump)) {
    RebindButton("Jump Key", m_isRebindingJumpKey, Config::TurboJumpKey);
    RenderBindModeSelector("Bind Mode", Config::TurboJumpMode);

    int jumpDelay = Config::TurboJumpDelayMs.load();
    FULL_SLIDER_INT(
        "Jump Spam Delay", jumpDelay, 1, 100, "%dms",
        (Config::TurboJumpDelayMs.store(jumpDelay), Config::SaveConfig()));

    int jumpDuration = Config::TurboJumpDurationMs.load();
    FULL_SLIDER_INT("Jump Hold Duration", jumpDuration, 0, 50, "%dms",
                    (Config::TurboJumpDurationMs.store(jumpDuration),
                     Config::SaveConfig()));
    EndFeatureChildren();
  }

#undef FULL_SLIDER_INT

  ImGui::Spacing();
  ImGui::TextDisabled("Superglide");
  ImGui::Separator();

  bool useSuperglide = Config::EnableSuperglide.load();
  if (ImGui::Checkbox("Superglide", &useSuperglide)) {
    Config::EnableSuperglide.store(useSuperglide);
    Config::SaveConfig();
  }
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
    ImGui::SetTooltip("Automates the frame-perfect Jump -> Crouch sequence");

  if (BeginFeatureChildren(useSuperglide)) {
    RebindButton("Superglide Bind", m_isRebindingSuperglideKey,
                 Config::SuperglideBind);

    // Target FPS slider — stored as double, displayed via float local copy
    float fps = static_cast<float>(Config::TargetFPS.load());
    ImGui::Text("Target FPS");
    char fpsBuf[16];
    snprintf(fpsBuf, sizeof(fpsBuf), "%.0f", fps);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x -
                    ImGui::CalcTextSize(fpsBuf).x);
    ImGui::TextDisabled("%s", fpsBuf);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
        ImVec2(ImGui::GetStyle().FramePadding.x, 2.0f));
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::SliderFloat("##SuperglideFPS", &fps, 30.0f, 300.0f, "")) {
      Config::TargetFPS.store(static_cast<double>(fps));
      Config::SaveConfig();
    }
    ImGui::PopStyleVar();
    EndFeatureChildren();
  }

  ImGui::Spacing();
  ImGui::TextDisabled("Input Backend");
  ImGui::Separator();
  {
    // --- Cached interception availability check (refresh once per second) ---
    struct InterceptionStatus {
      enum class State {
        Unknown,
        DllMissing,
        DriverInactive,
        Available
      } state = State::Unknown;
      DWORD lastCheckTick = 0;
    };
    static InterceptionStatus s_icStatus;

    const DWORD now = GetTickCount();
    if ((now - s_icStatus.lastCheckTick) > 1000u) {
      s_icStatus.lastCheckTick = now;

      // 1. Is interception.dll present next to the exe?
      wchar_t exePath[MAX_PATH] = {};
      GetModuleFileNameW(nullptr, exePath, MAX_PATH);
      for (int k = static_cast<int>(wcslen(exePath)); k >= 0; --k) {
        if (exePath[k] == L'\\' || exePath[k] == L'/') {
          exePath[k + 1] = L'\0';
          break;
        }
      }
      wchar_t dllPath[MAX_PATH] = {};
      wcscpy_s(dllPath, exePath);
      wcscat_s(dllPath, L"interception.dll");

      const bool dllPresent =
          (GetFileAttributesW(dllPath) != INVALID_FILE_ATTRIBUTES);

      if (!dllPresent) {
        s_icStatus.state = InterceptionStatus::State::DllMissing;
      } else {
        // 2. Try to create a driver context — confirms the kernel filter is
        //    active. We LoadLibrary temporarily; drivers loaded in both the
        //    target app and here share the same user-mode proxy so this is
        //    safe.
        HMODULE lib = LoadLibraryW(dllPath);
        if (!lib) {
          s_icStatus.state = InterceptionStatus::State::DriverInactive;
        } else {
          using CreateCtxFn = void *(*)();
          using DestroyCtxFn = void (*)(void *);
          auto createCtx = reinterpret_cast<CreateCtxFn>(
              GetProcAddress(lib, "interception_create_context"));
          auto destroyCtx = reinterpret_cast<DestroyCtxFn>(
              GetProcAddress(lib, "interception_destroy_context"));
          if (!createCtx || !destroyCtx) {
            s_icStatus.state = InterceptionStatus::State::DriverInactive;
          } else {
            void *ctx = createCtx();
            if (ctx) {
              destroyCtx(ctx);
              s_icStatus.state = InterceptionStatus::State::Available;
            } else {
              s_icStatus.state = InterceptionStatus::State::DriverInactive;
            }
          }
          FreeLibrary(lib);
        }
      }
    }

    // --- Status badge ---
    const char *icon = nullptr;
    const char *statusText = nullptr;
    ImVec4 statusColor{};
    switch (s_icStatus.state) {
    case InterceptionStatus::State::Available:
      icon = "[  OK  ]";
      statusText = "Interception driver ready";
      statusColor = ImVec4(0.25f, 0.85f, 0.25f, 1.0f);
      break;
    case InterceptionStatus::State::DllMissing:
      icon = "[  X   ]";
      statusText = "interception.dll not found next to exe";
      statusColor = ImVec4(0.90f, 0.30f, 0.30f, 1.0f);
      break;
    case InterceptionStatus::State::DriverInactive:
      icon = "[  !   ]";
      statusText = "DLL present but driver not installed / running";
      statusColor = ImVec4(0.95f, 0.70f, 0.10f, 1.0f);
      break;
    default:
      icon = "[  ?   ]";
      statusText = "Checking...";
      statusColor = ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
      break;
    }
    ImGui::TextColored(statusColor, "%s", icon);
    ImGui::SameLine();
    ImGui::TextColored(statusColor, "%s", statusText);
    ImGui::Spacing();

    // --- Radio buttons — Interception disabled if driver unavailable ---
    const bool driverReady =
        (s_icStatus.state == InterceptionStatus::State::Available);

    int backend = Config::SelectedBackend.load();
    bool changed = false;
    if (ImGui::RadioButton("WinHook (default)", &backend, 0))
      changed = true;
    ImGui::SameLine();
    if (!driverReady)
      ImGui::BeginDisabled();
    if (ImGui::RadioButton("Interception", &backend, 1))
      changed = true;
    if (!driverReady)
      ImGui::EndDisabled();

    if (changed) {
      Config::SelectedBackend.store(backend);
      SwitchBackend(static_cast<Config::InputBackendKind>(backend));
    }

    if (!driverReady && backend != 1) {
      ImGui::TextDisabled(
          "  Install Interception driver to enable this option.");
    }
  }

  ImGui::PopStyleVar(2);
}

void GuiManager::RenderConsoleContent() {
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 8));

  ImGui::TextDisabled("Delay Jitter");
  ImGui::Separator();
  {
    bool useJitter = Config::EnableJitter.load(std::memory_order_relaxed);
    if (ImGui::Checkbox("Enable Jitter", &useJitter)) {
      Config::EnableJitter.store(useJitter, std::memory_order_relaxed);
      Config::SaveConfig();
    }
    if (useJitter) {
      ImGui::Indent(16.0f);
      int jitterVal = Config::JitterMs.load(std::memory_order_relaxed);
      ImGui::Text("Jitter Range");
      char jBuf[16];
      snprintf(jBuf, sizeof(jBuf), "+/-%dms", jitterVal);
      ImGui::SameLine(ImGui::GetContentRegionAvail().x -
                      ImGui::CalcTextSize(jBuf).x);
      ImGui::TextDisabled("%s", jBuf);
      ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
          ImVec2(ImGui::GetStyle().FramePadding.x, 2.0f));
      ImGui::SetNextItemWidth(-FLT_MIN);
      if (ImGui::SliderInt("##JitterMs", &jitterVal, 1, 20, "")) {
        Config::JitterMs.store(jitterVal, std::memory_order_relaxed);
        Config::SaveConfig();
      }
      ImGui::PopStyleVar();
      ImGui::TextDisabled("Randomizes spam/turbo delays by +/- this amount");
      ImGui::Unindent(16.0f);
    }
  }

  ImGui::Spacing();
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
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 5));

  const ImVec4 colOn = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
  const ImVec4 colOff = ImVec4(0.5f, 0.5f, 0.5f, 1.0f);
  const ImVec4 colActive = ImVec4(0.4f, 0.85f, 1.0f, 1.0f);
  const ImVec4 colLabel = ImVec4(0.65f, 0.65f, 0.72f, 1.0f);

  auto StatusDot = [&](bool active) {
    ImGui::TextColored(active ? colOn : colOff, active ? "[ON] " : "[OFF]");
  };

  auto Tooltip = [](const char *text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
      ImGui::BeginTooltip();
      ImGui::PushTextWrapPos(280.0f);
      ImGui::TextUnformatted(text);
      ImGui::PopTextWrapPos();
      ImGui::EndTooltip();
    }
  };

  // ---- Input Backend ----
  ImGui::TextDisabled("Input Backend");
  ImGui::Separator();
  {
    const int backend = Config::SelectedBackend.load(std::memory_order_relaxed);
    const bool hookOk = (backend == 0)
                            ? (Globals::g_hHookThread != NULL)
                            : (Globals::g_hSuperglideThread != NULL ||
                               Globals::g_hSpamThread != NULL);
    ImGui::Text("Backend:");
    ImGui::SameLine();
    ImGui::TextColored(hookOk ? colOn : ImVec4(0.9f, 0.3f, 0.3f, 1.0f),
                       "%s", backend == 0 ? "WinHook" : "Interception");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)){     
      ImGui::SetTooltip(
        "The active input capture backend.\n"
        "WinHook uses a low-level keyboard hook.\n"
        "Interception uses a kernel-mode filter driver."
    );
    }
  }

  ImGui::Spacing();

  // ---- Movement Keys ----
  ImGui::TextDisabled("Movement Keys");
  ImGui::Separator();
  ImGui::Text("WASD:");
  for (int k : {'W', 'A', 'S', 'D'}) {
    bool down = Globals::g_KeyInfo[k].physicalKeyDown.load();
    ImGui::SameLine();
    ImGui::TextColored(down ? colOn : colOff, "%c", k);
  }

  ImGui::Spacing();

  // ---- Feature Status ----
  ImGui::TextDisabled("Feature Status");
  ImGui::Separator();

  // Lurch Strafing
  {
    const bool enabled = Config::EnableSpam.load(std::memory_order_relaxed);
    const bool active =
        enabled && Globals::g_isCSpamActive.load(std::memory_order_relaxed);
    StatusDot(active);
    ImGui::SameLine();
    ImGui::TextColored(enabled ? colLabel : colOff, "Lurch Strafing");
    if (active) {
      ImGui::Indent(16.0f);
      EnterCriticalSection(&Globals::g_csActiveKeys);
      const auto keys = Globals::g_activeSpamKeys;
      LeaveCriticalSection(&Globals::g_csActiveKeys);
      if (!keys.empty()) {
        ImGui::TextColored(colActive, "Spamming:");
        for (int vk : keys) {
          ImGui::SameLine();
          ImGui::Text("%c", static_cast<char>(vk));
        }
      }
      ImGui::Unindent(16.0f);
    }
  }

  // SnapTap
  {
    const bool enabled = Config::EnableSnapTap.load(std::memory_order_relaxed);
    StatusDot(enabled);
    ImGui::SameLine();
    ImGui::TextColored(enabled ? colLabel : colOff, "SnapTap (SOCD)");
  }

  // Turbo Loot
  {
    const bool enabled = Config::EnableTurboLoot.load(std::memory_order_relaxed);
    const bool active = enabled && KeybindManager::IsTurboLootActive();
    StatusDot(active);
    ImGui::SameLine();
    ImGui::TextColored(enabled ? colLabel : colOff, "Turbo Loot");
    if (active) {
      ImGui::Indent(16.0f);
      ImGui::TextColored(colActive, "Spamming @ %dms",
                         Config::TurboLootDelayMs.load(std::memory_order_relaxed));
      ImGui::Unindent(16.0f);
    }
  }

  // Turbo Jump
  {
    const bool enabled = Config::EnableTurboJump.load(std::memory_order_relaxed);
    const bool active = enabled && KeybindManager::IsTurboJumpActive();
    StatusDot(active);
    ImGui::SameLine();
    ImGui::TextColored(enabled ? colLabel : colOff, "Turbo Jump");
    if (active) {
      ImGui::Indent(16.0f);
      ImGui::TextColored(colActive, "Spamming @ %dms",
                         Config::TurboJumpDelayMs.load(std::memory_order_relaxed));
      ImGui::Unindent(16.0f);
    }
  }

  // Superglide
  {
    const bool enabled = Config::EnableSuperglide.load(std::memory_order_relaxed);
    StatusDot(enabled);
    ImGui::SameLine();
    ImGui::TextColored(enabled ? colLabel : colOff, "Superglide");
  }

  // Jitter
  {
    const bool enabled = Config::EnableJitter.load(std::memory_order_relaxed);
    StatusDot(enabled);
    ImGui::SameLine();
    ImGui::TextColored(enabled ? colLabel : colOff, "Delay Jitter");
    if (enabled) {
      ImGui::Indent(16.0f);
      ImGui::TextColored(colActive, "+/-%dms",
                         Config::JitterMs.load(std::memory_order_relaxed));
      ImGui::Unindent(16.0f);
    }
  }

  ImGui::Spacing();

  // ---- Superglide Timing Stats ----
  ImGui::TextDisabled("Superglide Timing");
  ImGui::Separator();

  const auto &stats = Globals::g_superglideStats;
  const int totalCount = stats.count.load(std::memory_order_relaxed);

  if (totalCount == 0) {
    ImGui::TextColored(colOff, "No executions yet.");
    Tooltip("Trigger a superglide to see timing accuracy and success rate "
            "statistics here.");
  } else {
    const int historyLen =
        (totalCount < Globals::kSuperglideHistorySize)
            ? totalCount
            : Globals::kSuperglideHistorySize;

    double sumChance = 0.0;
    double lastChance = 0.0;
    double lastFrames = 0.0;
    double lastErrorMs = 0.0;
    double minError = 1e9;
    double maxError = -1e9;
    float chancePlot[Globals::kSuperglideHistorySize]{};

    const int wIdx = stats.writeIdx.load(std::memory_order_relaxed);
    for (int i = 0; i < historyLen; ++i) {
      const int ri =
          (wIdx - historyLen + i + Globals::kSuperglideHistorySize * 2) %
          Globals::kSuperglideHistorySize;
      const auto &r = stats.history[ri];
      sumChance += r.chancePercent;
      if (r.errorMs < minError)
        minError = r.errorMs;
      if (r.errorMs > maxError)
        maxError = r.errorMs;
      chancePlot[i] = static_cast<float>(r.chancePercent);
      if (i == historyLen - 1) {
        lastChance = r.chancePercent;
        lastFrames = r.elapsedFrames;
        lastErrorMs = r.errorMs;
      }
    }

    const double avgChance = sumChance / historyLen;

    ImGui::Text("Total Executions:");
    ImGui::SameLine();
    ImGui::TextColored(colActive, "%d", totalCount);

    ImGui::Text("Last:");
    ImGui::SameLine();
    const ImVec4 chanceCol =
        (lastChance >= 90.0) ? colOn
        : (lastChance >= 50.0)
            ? ImVec4(0.95f, 0.85f, 0.20f, 1.0f)
            : ImVec4(0.95f, 0.40f, 0.30f, 1.0f);
    ImGui::TextColored(chanceCol, "%.1f%% chance", lastChance);
    ImGui::SameLine();
    ImGui::TextColored(colLabel, "(%.3f frames, %+.2fms error)",
                       lastFrames, lastErrorMs);
    Tooltip("Success probability of the most recent superglide execution. "
            "Based on the Apex Superglide Practice Tool formula: "
            "1 frame = 100%%, 0 or 2 frames = 0%%.");

    ImGui::Text("Average (last %d):", historyLen);
    ImGui::SameLine();
    const ImVec4 avgCol =
        (avgChance >= 90.0) ? colOn
        : (avgChance >= 50.0)
            ? ImVec4(0.95f, 0.85f, 0.20f, 1.0f)
            : ImVec4(0.95f, 0.40f, 0.30f, 1.0f);
    ImGui::TextColored(avgCol, "%.1f%%", avgChance);

    ImGui::Text("Error Range:");
    ImGui::SameLine();
    ImGui::TextColored(colLabel, "%+.2fms to %+.2fms", minError, maxError);
    Tooltip("Timing error relative to the ideal 1-frame delay. "
            "Negative = crouch fired early, positive = late. "
            "Closer to 0 is better.");

    ImGui::Spacing();
    ImGui::TextDisabled("Success History");
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                          ImVec4(0.35f, 0.75f, 0.35f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,
                          ImVec4(0.12f, 0.14f, 0.20f, 1.0f));
    ImGui::PlotHistogram("##SGHistory", chancePlot, historyLen, 0, nullptr,
                         0.0f, 100.0f, ImVec2(-FLT_MIN, 50));
    ImGui::PopStyleColor(2);
    Tooltip("Per-execution success probability. Each bar represents one "
            "superglide. Height = chance percentage (100%% = perfect).");
  }

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
