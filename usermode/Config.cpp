#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "Config.h"
#include "KeyNames.h"
#include "Utils.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <limits>
#include <string>

namespace {
[[nodiscard]] std::string Trim(const std::string &s) {
  const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
  auto begin = std::find_if_not(s.begin(), s.end(), isSpace);
  if (begin == s.end()) {
    return {};
  }
  auto end = std::find_if_not(s.rbegin(), s.rend(), isSpace).base();
  return std::string(begin, end);
}

[[nodiscard]] std::string Lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return s;
}

[[nodiscard]] bool ParseBool(const std::string &raw, bool &out) {
  const std::string v = Lower(raw);
  if (v == "1" || v == "true" || v == "yes" || v == "on") {
    out = true;
    return true;
  }
  if (v == "0" || v == "false" || v == "no" || v == "off") {
    out = false;
    return true;
  }
  return false;
}

[[nodiscard]] bool ParseUInt(const std::string &raw, uint32_t &out) {
  try {
    const unsigned long value = std::stoul(raw, nullptr, 10);
    if (value > std::numeric_limits<uint32_t>::max()) {
      return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
  } catch (...) {
    return false;
  }
}

[[nodiscard]] bool WriteDefaultConfig(const std::wstring &path,
                                      const RuntimeConfig &cfg) {
  std::ofstream out(path, std::ios::trunc);
  if (!out.is_open()) {
    return false;
  }

  out << "# NeoStrafe Configuration\n\n";
  out << "enabled = " << (cfg.enabled ? "true" : "false") << "\n";
  out << "snaptap = " << (cfg.snaptapEnabled ? "true" : "false") << "\n\n";

  out << "# Trigger key\n";
  out << "trigger = " << KeyNames::ScancodeToKeyName(cfg.triggerScanCode)
      << "\n\n";

  out << "# Spam timing (microseconds)\n";
  out << "spam_down_us = " << cfg.spamDownUs << "\n";
  out << "spam_up_us   = " << cfg.spamUpUs << "\n\n";

  out << "# Movement keys\n";
  out << "forward = " << KeyNames::ScancodeToKeyName(cfg.forwardScanCode)
      << "\n";
  out << "left    = " << KeyNames::ScancodeToKeyName(cfg.leftScanCode) << "\n";
  out << "back    = " << KeyNames::ScancodeToKeyName(cfg.backScanCode) << "\n";
  out << "right   = " << KeyNames::ScancodeToKeyName(cfg.rightScanCode)
      << "\n\n";

  out << "# Input backend\n";
  // If we have a backend set, write it. Otherwise comment it out so user is
  // prompted or defaults. User asked for: input_backend = interception But if
  // we are writing default, we might not have prompted yet? The user request
  // example says: "input_backend = interception" So we will write it.
  if (cfg.hasInputBackend) {
    out << "input_backend = " << Config::BackendKindToString(cfg.inputBackend)
        << "\n";
  } else {
    out << "input_backend = interception\n";
  }

  return true;
}
} // namespace

const char *Config::BackendKindToString(InputBackendKind kind) noexcept {
  switch (kind) {
  case InputBackendKind::Interception:
    return "interception";
  case InputBackendKind::KeyboardHook:
    return "kbdhook";
  case InputBackendKind::RawInput:
    return "rawinput";
  default:
    return "interception";
  }
}

bool Config::BackendKindFromString(const std::string &text,
                                   InputBackendKind &out) noexcept {
  const std::string value = Lower(Trim(text));
  if (value == "interception") {
    out = InputBackendKind::Interception;
    return true;
  }
  if (value == "kbdhook" || value == "keyboardhook" || value == "hook") {
    out = InputBackendKind::KeyboardHook;
    return true;
  }
  if (value == "rawinput" || value == "raw") {
    out = InputBackendKind::RawInput;
    return true;
  }
  return false;
}

bool Config::Load(RuntimeConfig &out, const std::wstring &path) noexcept {
  out.hasInputBackend = false;

  std::ifstream in(path);
  if (!in.is_open()) {
    if (!WriteDefaultConfig(path, out)) {
      Utils::LogError("[Config] failed to create default config.cfg");
      return false;
    }
    Utils::LogInfo("config.cfg created (default)");
    return true;
  }

  std::string line;
  uint32_t lineNo = 0;
  while (std::getline(in, line)) {
    ++lineNo;
    const std::string trimmed = Trim(line);
    if (trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';') {
      continue;
    }

    const size_t eq = trimmed.find('=');
    if (eq == std::string::npos) {
      Utils::LogError("[Config] malformed line %u ignored: %s", lineNo,
                      trimmed.c_str());
      continue;
    }

    const std::string key = Lower(Trim(trimmed.substr(0, eq)));
    const std::string value = Trim(trimmed.substr(eq + 1));
    if (key.empty() || value.empty()) {
      continue;
    }

    if (key == "enabled" || key == "iswasdstrafingenabled") {
      bool parsed = out.enabled;
      if (ParseBool(value, parsed)) {
        out.enabled = parsed;
      }
      continue;
    }

    if (key == "snaptap" || key == "issnaptapenabled") {
      bool parsed = out.snaptapEnabled;
      if (ParseBool(value, parsed)) {
        out.snaptapEnabled = parsed;
      }
      continue;
    }

    if (key == "islocked") {
      bool parsed = out.isLocked;
      if (ParseBool(value, parsed)) {
        out.isLocked = parsed;
      }
      continue;
    }

    if (key == "trigger" || key == "key_spam_trigger") {
      const uint16_t sc = KeyNames::KeyNameToScancode(value);
      if (sc != 0) {
        out.triggerScanCode = sc;
      }
      continue;
    }

    if (key == "spam_down_us") {
      uint32_t parsed = out.spamDownUs;
      if (ParseUInt(value, parsed)) {
        out.spamDownUs = parsed;
      }
      continue;
    }

    if (key == "spam_up_us") {
      uint32_t parsed = out.spamUpUs;
      if (ParseUInt(value, parsed)) {
        out.spamUpUs = parsed;
      }
      continue;
    }

    // Backward compatibility for old millisecond-based keys.
    if (key == "spam_key_down_duration") {
      uint32_t parsedMs = 0;
      if (ParseUInt(value, parsedMs)) {
        out.spamDownUs = parsedMs * 1000u;
      }
      continue;
    }
    if (key == "spam_delay_ms") {
      uint32_t parsedMs = 0;
      if (ParseUInt(value, parsedMs)) {
        out.spamUpUs = parsedMs * 1000u;
      }
      continue;
    }

    if (key == "forward") {
      const uint16_t sc = KeyNames::KeyNameToScancode(value);
      if (sc != 0) {
        out.forwardScanCode = sc;
      }
      continue;
    }
    if (key == "left") {
      const uint16_t sc = KeyNames::KeyNameToScancode(value);
      if (sc != 0) {
        out.leftScanCode = sc;
      }
      continue;
    }
    if (key == "back") {
      const uint16_t sc = KeyNames::KeyNameToScancode(value);
      if (sc != 0) {
        out.backScanCode = sc;
      }
      continue;
    }
    if (key == "right") {
      const uint16_t sc = KeyNames::KeyNameToScancode(value);
      if (sc != 0) {
        out.rightScanCode = sc;
      }
      continue;
    }

    if (key == "input_backend") {
      InputBackendKind parsed = out.inputBackend;
      if (BackendKindFromString(value, parsed)) {
        out.inputBackend = parsed;
        out.hasInputBackend = true;
      }
      continue;
    }
  }

  Utils::LogInfo("config.cfg loaded");
  return true;
}

bool Config::Save(const RuntimeConfig &cfg, const std::wstring &path) noexcept {
  std::ofstream out(path, std::ios::trunc);
  if (!out.is_open()) {
    Utils::LogError("[Config] failed to write config.cfg");
    return false;
  }

  out << "enabled = " << (cfg.enabled ? "true" : "false") << '\n';
  out << "snaptap = " << (cfg.snaptapEnabled ? "true" : "false") << '\n';
  out << "trigger = " << KeyNames::ScancodeToKeyName(cfg.triggerScanCode)
      << '\n';
  out << "spam_down_us = " << cfg.spamDownUs << '\n';
  out << "spam_up_us = " << cfg.spamUpUs << '\n';
  out << '\n';
  out << "forward = " << KeyNames::ScancodeToKeyName(cfg.forwardScanCode)
      << '\n';
  out << "left = " << KeyNames::ScancodeToKeyName(cfg.leftScanCode) << '\n';
  out << "back = " << KeyNames::ScancodeToKeyName(cfg.backScanCode) << '\n';
  out << "right = " << KeyNames::ScancodeToKeyName(cfg.rightScanCode) << '\n';
  out << '\n';
  out << "input_backend = " << BackendKindToString(cfg.inputBackend) << '\n';

  return true;
}
