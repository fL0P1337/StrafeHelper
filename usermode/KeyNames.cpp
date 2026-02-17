#include "KeyNames.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string_view>

namespace {
struct KeyEntry {
  const char *name;
  uint16_t scancode;
};

constexpr std::array<KeyEntry, 92> kKeyMap{{
    {"A", 0x1E},         {"B", 0x30},         {"C", 0x2E},
    {"D", 0x20},         {"E", 0x12},         {"F", 0x21},
    {"G", 0x22},         {"H", 0x23},         {"I", 0x17},
    {"J", 0x24},         {"K", 0x25},         {"L", 0x26},
    {"M", 0x32},         {"N", 0x31},         {"O", 0x18},
    {"P", 0x19},         {"Q", 0x10},         {"R", 0x13},
    {"S", 0x1F},         {"T", 0x14},         {"U", 0x16},
    {"V", 0x2F},         {"W", 0x11},         {"X", 0x2D},
    {"Y", 0x15},         {"Z", 0x2C},         {"0", 0x0B},
    {"1", 0x02},         {"2", 0x03},         {"3", 0x04},
    {"4", 0x05},         {"5", 0x06},         {"6", 0x07},
    {"7", 0x08},         {"8", 0x09},         {"9", 0x0A},
    {"F1", 0x3B},        {"F2", 0x3C},        {"F3", 0x3D},
    {"F4", 0x3E},        {"F5", 0x3F},        {"F6", 0x40},
    {"F7", 0x41},        {"F8", 0x42},        {"F9", 0x43},
    {"F10", 0x44},       {"F11", 0x57},       {"F12", 0x58},
    {"CTRL", 0x1D},      {"CONTROL", 0x1D},   {"ALT", 0x38},
    {"SHIFT", 0x2A},     {"SPACE", 0x39},     {"TAB", 0x0F},
    {"ESC", 0x01},       {"ESCAPE", 0x01},    {"ENTER", 0x1C},
    {"RETURN", 0x1C},    {"BACKSPACE", 0x0E}, {"CAPSLOCK", 0x3A},
    {"UP", 0x48},        {"DOWN", 0x50},      {"LEFT", 0x4B},
    {"RIGHT", 0x4D},     {"INSERT", 0x52},    {"DELETE", 0x53},
    {"HOME", 0x47},      {"END", 0x4F},       {"PGUP", 0x49},
    {"PGDN", 0x51},      {"LWIN", 0x5B},      {"RWIN", 0x5C},
    {"APPS", 0x5D},      {"NUMLOCK", 0x45},   {"SCROLLLOCK", 0x46},
    {"LSHIFT", 0x2A},    {"RSHIFT", 0x36},    {"LCTRL", 0x1D},
    {"RCTRL", 0x1D}, // Maps to same base scancode usually for Engine
    {"LALT", 0x38},      {"RALT", 0x38},      {"MINUS", 0x0C},
    {"EQUALS", 0x0D},    {"LBRACKET", 0x1A},  {"RBRACKET", 0x1B},
    {"SEMICOLON", 0x27}, {"QUOTE", 0x28},     {"BACKQUOTE", 0x29},
    {"BACKSLASH", 0x2B}, {"COMMA", 0x33},     {"PERIOD", 0x34},
    {"SLASH", 0x35},
}};

[[nodiscard]] std::string Normalize(const std::string &in) {
  std::string out;
  out.reserve(in.size());
  for (const unsigned char c : in) {
    if (std::isspace(c) != 0 || c == '_') {
      continue;
    }
    out.push_back(static_cast<char>(std::toupper(c)));
  }
  return out;
}
} // namespace

uint16_t KeyNames::KeyNameToScancode(const std::string &name) noexcept {
  const std::string normalized = Normalize(name);
  if (normalized.empty()) {
    return 0;
  }

  if (normalized.rfind("0X", 0) == 0) {
    try {
      const unsigned long parsed = std::stoul(normalized, nullptr, 16);
      if (parsed <= 0xFFFFu) {
        return static_cast<uint16_t>(parsed);
      }
    } catch (...) {
      return 0;
    }
    return 0;
  }

  const auto it =
      std::find_if(kKeyMap.begin(), kKeyMap.end(), [&](const KeyEntry &entry) {
        return normalized == entry.name;
      });
  if (it != kKeyMap.end()) {
    return it->scancode;
  }

  return 0;
}

std::string KeyNames::ScancodeToKeyName(uint16_t scanCode) {
  const auto it =
      std::find_if(kKeyMap.begin(), kKeyMap.end(), [&](const KeyEntry &entry) {
        return entry.scancode == scanCode;
      });
  if (it != kKeyMap.end()) {
    return it->name;
  }

  char buf[16] = {};
  std::snprintf(buf, sizeof(buf), "SC_0x%02X", scanCode & 0xFFu);
  return std::string(buf);
}
