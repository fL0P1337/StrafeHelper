// Utils.cpp
#include "Utils.h"
#include "Config.h"
#include <array>
#include <iostream>
#include <random>
#include <windows.h> // For FormatMessage, LocalFree

DWORD ApplyJitter(DWORD baseDelay) {
  if (!Config::EnableJitter.load(std::memory_order_relaxed))
    return baseDelay;
  const int jitter = Config::JitterMs.load(std::memory_order_relaxed);
  if (jitter <= 0)
    return baseDelay;
  thread_local std::mt19937 rng(std::random_device{}());
  std::uniform_int_distribution<int> dist(-jitter, jitter);
  const int adjusted = static_cast<int>(baseDelay) + dist(rng);
  return static_cast<DWORD>(adjusted < 1 ? 1 : adjusted);
}

WORD VirtualKeyToScanCode(int vk) {
  UINT scanCode =
      MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC_EX);
  if (scanCode == 0) {
    scanCode = MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
  }
  return static_cast<WORD>(scanCode & 0xFFu);
}

DWORD VirtualKeyInputFlags(int vk, bool keyDown) {
  const UINT scanCode =
      MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC_EX);
  DWORD flags = KEYEVENTF_SCANCODE;
  if ((scanCode & 0xFF00u) == 0xE000u) {
    flags |= KEYEVENTF_EXTENDEDKEY;
  }
  if (!keyDown) {
    flags |= KEYEVENTF_KEYUP;
  }
  return flags;
}

std::string FormatVirtualKeyName(int vk) {
  struct KeyName {
    int vk;
    const char *name;
  };
  static constexpr std::array<KeyName, 18> kNames{{
      {VK_LCONTROL, "Left Ctrl"},
      {VK_RCONTROL, "Right Ctrl"},
      {VK_CONTROL, "Ctrl"},
      {VK_LSHIFT, "Left Shift"},
      {VK_RSHIFT, "Right Shift"},
      {VK_SHIFT, "Shift"},
      {VK_LMENU, "Left Alt"},
      {VK_RMENU, "Right Alt"},
      {VK_MENU, "Alt"},
      {VK_SPACE, "Space"},
      {VK_TAB, "Tab"},
      {VK_ESCAPE, "Escape"},
      {VK_RETURN, "Enter"},
      {VK_BACK, "Backspace"},
      {VK_LBUTTON, "Mouse Left"},
      {VK_RBUTTON, "Mouse Right"},
      {VK_MBUTTON, "Mouse Middle"},
      {VK_XBUTTON1, "Mouse X1"},
  }};

  if (vk == VK_XBUTTON2) {
    return "Mouse X2";
  }
  if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
    return std::string(1, static_cast<char>(vk));
  }
  for (const auto &entry : kNames) {
    if (entry.vk == vk) {
      return entry.name;
    }
  }

  const UINT scanCode =
      MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC_EX);
  if (scanCode != 0) {
    char name[64]{};
    LONG lParam = static_cast<LONG>((scanCode & 0xFFu) << 16);
    if ((scanCode & 0xFF00u) == 0xE000u) {
      lParam |= (1L << 24);
    }
    if (GetKeyNameTextA(lParam, name, static_cast<int>(sizeof(name))) > 0) {
      return name;
    }
  }

  return "VK " + std::to_string(vk);
}

void LogError(const std::string& message, DWORD errorCode) {
    std::cerr << "ERROR: " << message;
    if (errorCode == 0) {
        errorCode = GetLastError(); // Get error code if not provided
    }
    if (errorCode != 0) {
        LPSTR messageBuffer = nullptr;
        size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

        if (size > 0 && messageBuffer != nullptr) {
            std::string systemMessage(messageBuffer, size);
            while (!systemMessage.empty() && (systemMessage.back() == '\n' || systemMessage.back() == '\r')) {
                systemMessage.pop_back();
            }
            std::cerr << " (Code: " << errorCode << " - " << systemMessage << ")";
        }
        else {
            std::cerr << " (Code: " << errorCode << " - FormatMessage failed with code " << GetLastError() << ")";
        }
        LocalFree(messageBuffer);
    }
    std::cerr << std::endl;

#ifndef _DEBUG
    // Optional: Show message box in Release builds
    // std::string fullMsg = message + " (Error Code: " + std::to_string(errorCode) + ")";
    // MessageBoxA(NULL, fullMsg.c_str(), "StrafeHelper Error", MB_OK | MB_ICONERROR);
#endif
}

std::wstring GetExecutableDirectory() {
    wchar_t buffer[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return L"";
    }

    std::wstring path(buffer, length);
    size_t pos = path.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        return path.substr(0, pos + 1);
    }
    return L"";
}