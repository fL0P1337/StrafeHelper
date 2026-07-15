// Config.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Config.h"
#include "Logger.h"
#include "Utils.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Config {
namespace {
std::mutex g_configIoMutex;

std::string TrimCopy(const std::string& value) {
  const size_t first = value.find_first_not_of(" \t\n\r\f\v");
  if (first == std::string::npos) {
    return "";
  }
  const size_t last = value.find_last_not_of(" \t\n\r\f\v");
  return value.substr(first, last - first + 1);
}

std::string NormalizeKeyName(const std::string& value) {
  std::string trimmed = TrimCopy(value);
  std::string normalized;
  normalized.reserve(trimmed.size());
  for (unsigned char ch : trimmed) {
    if (ch == ' ' || ch == '_' || ch == '-') {
      continue;
    }
    normalized.push_back(static_cast<char>(std::tolower(ch)));
  }
  return normalized;
}

bool ParseBoolValue(const std::string &value) {
  std::string normalized = TrimCopy(value);
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  if (normalized == "true" || normalized == "1") {
    return true;
  }
  if (normalized == "false" || normalized == "0") {
    return false;
  }
  throw std::invalid_argument("expected true/false/1/0");
}

int ParseKeyValue(const std::string &value, int fallback) {
  const std::string trimmed = TrimCopy(value);
  if (trimmed.empty()) {
    return fallback;
  }
  const std::string normalized = NormalizeKeyName(trimmed);
  if (normalized == "none" || normalized == "unbound") {
    return 0;
  }
  if (trimmed.length() == 1 &&
      std::isalnum(static_cast<unsigned char>(trimmed[0]))) {
    return static_cast<int>(
        std::toupper(static_cast<unsigned char>(trimmed[0])));
  }

  const std::string key = normalized;
  struct KeyAlias {
    const char *name;
    int vk;
  };
  static constexpr KeyAlias kAliases[] = {
      {"ctrl", VK_LCONTROL},      {"control", VK_LCONTROL},
      {"lctrl", VK_LCONTROL},     {"leftctrl", VK_LCONTROL},
      {"leftcontrol", VK_LCONTROL},
      {"rctrl", VK_RCONTROL},     {"rightctrl", VK_RCONTROL},
      {"rightcontrol", VK_RCONTROL},
      {"shift", VK_SHIFT},        {"lshift", VK_LSHIFT},
      {"leftshift", VK_LSHIFT},   {"rshift", VK_RSHIFT},
      {"rightshift", VK_RSHIFT},  {"alt", VK_LMENU},
      {"lalt", VK_LMENU},         {"leftalt", VK_LMENU},
      {"ralt", VK_RMENU},         {"rightalt", VK_RMENU},
      {"space", VK_SPACE},        {"spacebar", VK_SPACE},
      {"tab", VK_TAB},            {"enter", VK_RETURN},
      {"return", VK_RETURN},      {"escape", VK_ESCAPE},
      {"esc", VK_ESCAPE},         {"backspace", VK_BACK},
      {"xbutton1", VK_XBUTTON1},  {"mousex1", VK_XBUTTON1},
      {"mouse4", VK_XBUTTON1},    {"xbutton2", VK_XBUTTON2},
      {"mousex2", VK_XBUTTON2},   {"mouse5", VK_XBUTTON2},
  };
  for (const auto &alias : kAliases) {
    if (key == alias.name) {
      return alias.vk;
    }
  }

  try {
    return std::stoi(trimmed, nullptr, 0);
  } catch (...) {
    return fallback;
  }
}

std::string FormatConfigKey(int vk) {
  if (vk == 0) {
    return "None";
  }
  if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
    return std::string(1, static_cast<char>(vk));
  }

  switch (vk) {
  case VK_CONTROL:
    return "Ctrl";
  case VK_LCONTROL:
    return "LCtrl";
  case VK_RCONTROL:
    return "RCtrl";
  case VK_SHIFT:
    return "Shift";
  case VK_LSHIFT:
    return "LShift";
  case VK_RSHIFT:
    return "RShift";
  case VK_MENU:
    return "Alt";
  case VK_LMENU:
    return "LAlt";
  case VK_RMENU:
    return "RAlt";
  case VK_SPACE:
    return "Space";
  case VK_XBUTTON1:
    return "XButton1";
  case VK_XBUTTON2:
    return "XButton2";
  default:
    return std::to_string(vk);
  }
}
void ValidateConfigValues() {
  auto clampInt = [](std::atomic<int> &value, int minimum, int maximum,
                     const char *name) {
    const int current = value.load(std::memory_order_relaxed);
    const int corrected = std::clamp(current, minimum, maximum);
    if (corrected != current) {
      value.store(corrected, std::memory_order_relaxed);
      Logger::GetInstance().Log(std::string("Warning: Clamped ") + name +
                                " to " + std::to_string(corrected));
    }
  };

  clampInt(SpamDelayMs, 0, 1000, "spam_delay_ms");
  clampInt(SpamKeyDownDurationMs, 0, 1000, "spam_key_down_duration");
  clampInt(TurboLootDelayMs, 0, 1000, "turbo_loot_delay");
  clampInt(TurboLootDurationMs, 0, 1000, "turbo_loot_duration");
  clampInt(TurboJumpDelayMs, 0, 1000, "turbo_jump_delay");
  clampInt(TurboJumpDurationMs, 0, 1000, "turbo_jump_duration");
  clampInt(JitterMs, 0, 1000, "jitter_ms");
  clampInt(DebounceUs, 0, 1000000, "debounce_us");

  const double fps = TargetFPS.load(std::memory_order_relaxed);
  if (!std::isfinite(fps) || fps < 30.0 || fps > 300.0) {
    TargetFPS.store(60.0, std::memory_order_relaxed);
    Logger::GetInstance().Log(
        "Warning: target_fps must be finite and within 30..300; reset to 60.");
  }

  struct Binding {
    std::atomic<int> *key;
    std::atomic<bool> *enabled;
    int fallback;
    const char *name;
  };
  Binding bindings[] = {
      {&KeySpamTrigger, &EnableSpam, 'C', "Lurch Trigger"},
      {&TurboLootKey, &EnableTurboLoot, 'E', "Turbo Loot"},
      {&TurboJumpKey, &EnableTurboJump, VK_SPACE, "Turbo Jump"},
      {&SuperglideBind, &EnableSuperglide, VK_OEM_3, "Superglide"},
  };

  for (auto &binding : bindings) {
    const int vk = binding.key->load(std::memory_order_relaxed);
    const bool enabled = binding.enabled->load(std::memory_order_relaxed);
    if (vk == 0 && !enabled) {
      continue;
    }
    if (vk <= 0 || vk >= 256) {
      binding.key->store(binding.fallback, std::memory_order_relaxed);
      Logger::GetInstance().Log(std::string("Warning: Invalid keybind for ") +
                                binding.name + "; restored default.");
    }
  }

  for (size_t i = 0; i < std::size(bindings); ++i) {
    if (!bindings[i].enabled->load(std::memory_order_relaxed)) {
      continue;
    }
    const int vk = bindings[i].key->load(std::memory_order_relaxed);
    for (size_t j = 0; j < i; ++j) {
      if (bindings[j].enabled->load(std::memory_order_relaxed) &&
          bindings[j].key->load(std::memory_order_relaxed) == vk) {
        bindings[i].enabled->store(false, std::memory_order_relaxed);
        Logger::GetInstance().Log(std::string("Warning: Disabled ") +
                                  bindings[i].name + " because its key conflicts with " +
                                  bindings[j].name + ".");
        break;
      }
    }
  }
}
} // namespace

// --- Config key name constants — single source of truth used by Load+Save ---
namespace Keys {
  constexpr const char* kSpamDelayMs          = "spam_delay_ms";
  constexpr const char* kSpamKeyDownDuration   = "spam_key_down_duration";
  constexpr const char* kIsLocked              = "is_locked";
  constexpr const char* kEnableSpam            = "enable_spam";
  constexpr const char* kEnableSnapTap         = "enable_snaptap";
  constexpr const char* kKeySpamTrigger        = "key_spam_trigger";
  constexpr const char* kKeySpamTriggerMode    = "key_spam_trigger_mode";
  constexpr const char* kEnableTurboLoot       = "enable_turbo_loot";
  constexpr const char* kTurboLootKey          = "turbo_loot_key";
  constexpr const char* kTurboLootDelay        = "turbo_loot_delay";
  constexpr const char* kTurboLootDuration     = "turbo_loot_duration";
  constexpr const char* kTurboLootMode         = "turbo_loot_mode";
  constexpr const char* kEnableTurboJump       = "enable_turbo_jump";
  constexpr const char* kTurboJumpKey          = "turbo_jump_key";
  constexpr const char* kTurboJumpDelay        = "turbo_jump_delay";
  constexpr const char* kTurboJumpDuration     = "turbo_jump_duration";
  constexpr const char* kTurboJumpMode         = "turbo_jump_mode";
  constexpr const char* kInputBackend          = "input_backend";
  constexpr const char* kEnableSuperglide      = "enable_superglide";
  constexpr const char* kSuperglideBind        = "superglide_bind";
  constexpr const char* kTargetFps             = "target_fps";
  constexpr const char* kSuperglideMode        = "superglide_mode";
  constexpr const char* kEnableJitter          = "enable_jitter";
  constexpr const char* kJitterMs              = "jitter_ms";
  constexpr const char* kDebounceUs            = "debounce_us";
} // namespace Keys

// --- Definitions of extern variables from Config.h ---
const char APP_NAME[] = "StrafeHelper";
const char VERSION[] = "1.2.0";
const char CONFIG_FILE_NAME[] = "config.cfg";

const TCHAR WINDOW_CLASS_NAME[] = TEXT("StrafeHelperWindowClass");
const TCHAR WINDOW_TITLE[] = TEXT("StrafeHelper Hidden Window");

// Returns the full path to the config file, located next to the executable.
static std::string GetConfigFilePath() {
  const std::wstring dir = GetExecutableDirectory();
  if (dir.empty()) {
    return CONFIG_FILE_NAME; // Fallback to CWD
  }
#ifdef _WIN32
  // Convert the wide-char directory path to a narrow string.
  const int needed = WideCharToMultiByte(CP_ACP, 0, dir.c_str(), -1,
                                         nullptr, 0, nullptr, nullptr);
  if (needed <= 0) {
    return CONFIG_FILE_NAME;
  }
  std::string narrowDir(static_cast<size_t>(needed), '\0');
  WideCharToMultiByte(CP_ACP, 0, dir.c_str(), -1, narrowDir.data(), needed,
                      nullptr, nullptr);
  narrowDir.pop_back();
  return narrowDir + CONFIG_FILE_NAME;
#else
  // Non-Windows fallback: use CWD
  return CONFIG_FILE_NAME;
#endif
}

// Initialize atomics with defaults
std::atomic<int> SpamDelayMs{10};
std::atomic<int> SpamKeyDownDurationMs{5};
std::atomic<bool> IsLocked{false};
std::atomic<bool> EnableSpam{true};
std::atomic<bool> EnableSnapTap{true};
std::atomic<int> KeySpamTrigger{'C'}; // VK_KEY 'C'
std::atomic<KeybindMode> KeySpamTriggerMode{KeybindMode::Hold};
std::atomic<bool> KeySpamTriggerToggleActive{false};

// Turbo Loot
std::atomic<bool> EnableTurboLoot{false};
std::atomic<int> TurboLootKey{0x45}; // 'E'
std::atomic<int> TurboLootDelayMs{15};
std::atomic<int> TurboLootDurationMs{5};
std::atomic<KeybindMode> TurboLootMode{KeybindMode::Hold};
std::atomic<bool> TurboLootToggleActive{false};

// Turbo Jump
std::atomic<bool> EnableTurboJump{false};
std::atomic<int> TurboJumpKey{0x20}; // VK_SPACE
std::atomic<int> TurboJumpDelayMs{15};
std::atomic<int> TurboJumpDurationMs{5};
std::atomic<KeybindMode> TurboJumpMode{KeybindMode::Hold};
std::atomic<bool> TurboJumpToggleActive{false};

// Input backend (0 = KbdHook default)
std::atomic<int> SelectedBackend{0};

// Superglide
std::atomic<bool> EnableSuperglide{false};
std::atomic<int> SuperglideBind{0xC0}; // VK_OEM_3 = tilde ~
std::atomic<double> TargetFPS{60.0};
std::atomic<KeybindMode> SuperglideMode{KeybindMode::Hold};
std::atomic<bool> SuperglideToggleActive{false};

// Delay jitter
std::atomic<bool> EnableJitter{false};
std::atomic<int> JitterMs{3};

// Edge-debounce window in microseconds (per VK). 0 disables.
std::atomic<int> DebounceUs{500};

void ValidateConfig() {
  ValidateConfigValues();
}

// --- Implementation of LoadConfig ---
void LoadConfig() {
  std::lock_guard<std::mutex> ioLock(g_configIoMutex);
  const std::string configPath = GetConfigFilePath();
  std::ifstream configFile(configPath);
  if (!configFile.is_open()) {
    std::string logMsg = "Config file '" + configPath +
                         "' not found. Using defaults.\n";
    logMsg += " Defaults: Delay=" + std::to_string(SpamDelayMs.load()) +
              "ms, Duration=" + std::to_string(SpamKeyDownDurationMs.load()) +
              "ms, Trigger=" + FormatVirtualKeyName(KeySpamTrigger.load()) +
              ", Spam=" + (EnableSpam.load() ? "true" : "false") +
              ", SnapTap=" + (EnableSnapTap.load() ? "true" : "false");
    Logger::GetInstance().Log(logMsg);
    return;
  }

  Logger::GetInstance().Log("Loading configuration from " + configPath + "...");
  std::string line;
  int lineNumber = 0;
  bool hasEnableSpam = false;
  bool legacyEnableSpam = EnableSpam.load(std::memory_order_relaxed);
  while (std::getline(configFile, line)) {
    lineNumber++;
    std::string trimmedLine;
    size_t first = line.find_first_not_of(" \t\n\r\f\v");
    if (first == std::string::npos || line[first] == '#') {
      continue;
    }
    size_t last = line.find_last_not_of(" \t\n\r\f\v");
    trimmedLine = line.substr(first, (last - first + 1));

    size_t equalsPos = trimmedLine.find('=');
    if (equalsPos == std::string::npos) {
      Logger::GetInstance().Log("Warning: Malformed line " +
                                std::to_string(lineNumber) +
                                " in config: " + line);
      continue;
    }

    std::string key = trimmedLine.substr(0, equalsPos);
    std::string value = trimmedLine.substr(equalsPos + 1);
    // Trim key/value
    size_t key_last = key.find_last_not_of(" \t\n\r\f\v");
    if (key_last != std::string::npos)
      key = key.substr(0, key_last + 1);
    size_t value_first = value.find_first_not_of(" \t\n\r\f\v");
    if (value_first != std::string::npos)
      value = value.substr(value_first);

    try {
      // Legacy key migration
      if (key == "isWASDStrafingEnabled") {
        legacyEnableSpam = ParseBoolValue(value);
      }
      // Legacy SCREAMING_SNAKE keys (kept for backward compat)
      else if (key == "SPAM_DELAY_MS" || key == Keys::kSpamDelayMs)
        SpamDelayMs = std::stoi(value);
      else if (key == "SPAM_KEY_DOWN_DURATION" || key == Keys::kSpamKeyDownDuration)
        SpamKeyDownDurationMs = std::stoi(value);
      else if (key == "isLocked" || key == Keys::kIsLocked)
        IsLocked = ParseBoolValue(value);
      else if (key == Keys::kEnableSpam) {
        EnableSpam = ParseBoolValue(value);
        hasEnableSpam = true;
      }
      else if (key == Keys::kEnableSnapTap)
        EnableSnapTap = ParseBoolValue(value);
      else if (key == "KEY_SPAM_TRIGGER" || key == Keys::kKeySpamTrigger) {
        if (!value.empty())
          KeySpamTrigger = ParseKeyValue(value, KeySpamTrigger.load(std::memory_order_relaxed));
      }
      else if (key == Keys::kKeySpamTriggerMode) {
        int mode = std::stoi(value);
        KeySpamTriggerMode = (mode == 1) ? KeybindMode::Toggle : KeybindMode::Hold;
      }
      // Turbo Loot
      else if (key == Keys::kEnableTurboLoot)
        EnableTurboLoot = ParseBoolValue(value);
      else if (key == Keys::kTurboLootKey)
        TurboLootKey = ParseKeyValue(value, TurboLootKey.load(std::memory_order_relaxed));
      else if (key == Keys::kTurboLootDelay)
        TurboLootDelayMs = std::stoi(value);
      else if (key == Keys::kTurboLootDuration)
        TurboLootDurationMs = std::stoi(value);
      else if (key == Keys::kTurboLootMode) {
        int mode = std::stoi(value);
        TurboLootMode = (mode == 1) ? KeybindMode::Toggle : KeybindMode::Hold;
      }
      // Turbo Jump
      else if (key == Keys::kEnableTurboJump)
        EnableTurboJump = ParseBoolValue(value);
      else if (key == Keys::kTurboJumpKey)
        TurboJumpKey = ParseKeyValue(value, TurboJumpKey.load(std::memory_order_relaxed));
      else if (key == Keys::kTurboJumpDelay)
        TurboJumpDelayMs = std::stoi(value);
      else if (key == Keys::kTurboJumpDuration)
        TurboJumpDurationMs = std::stoi(value);
      else if (key == Keys::kTurboJumpMode) {
        int mode = std::stoi(value);
        TurboJumpMode = (mode == 1) ? KeybindMode::Toggle : KeybindMode::Hold;
      }
      else if (key == Keys::kInputBackend) {
        int v = std::stoi(value);
        SelectedBackend = (v == 1) ? 1 : 0;
      }
      // Superglide
      else if (key == Keys::kEnableSuperglide)
        EnableSuperglide = ParseBoolValue(value);
      else if (key == Keys::kSuperglideBind)
        SuperglideBind = ParseKeyValue(value, SuperglideBind.load(std::memory_order_relaxed));
      else if (key == Keys::kTargetFps) {
        double fps = std::stod(value);
        if (fps > 0.0)
          TargetFPS.store(fps);
      }
      else if (key == Keys::kSuperglideMode) {
        // Legacy key retained for compatibility; superglide is always HOLD.
        (void)std::stoi(value);
        SuperglideMode = KeybindMode::Hold;
      }
      // Jitter
      else if (key == Keys::kEnableJitter)
        EnableJitter = ParseBoolValue(value);
      else if (key == Keys::kJitterMs) {
        int v = std::stoi(value);
        if (v >= 0)
          JitterMs = v;
      }
      else if (key == Keys::kDebounceUs) {
        int v = std::stoi(value);
        if (v >= 0)
          DebounceUs = v;
      }
    } catch (const std::exception &e) {
      Logger::GetInstance().Log("Warning: Invalid config value on line " +
                                std::to_string(lineNumber) + " for key '" +
                                key + "': " + value + " (" + e.what() + ")");
    }
  }
  configFile.close();

  if (!hasEnableSpam) {
    EnableSpam.store(legacyEnableSpam, std::memory_order_relaxed);
  }

  // Superglide mode is fixed to HOLD even if legacy config says otherwise.
  SuperglideMode.store(KeybindMode::Hold, std::memory_order_relaxed);
  SuperglideToggleActive.store(false, std::memory_order_relaxed);

  ValidateConfig();

  std::string logMsg = "Configuration loaded:\n";
  logMsg += "  SPAM_DELAY_MS: " + std::to_string(SpamDelayMs.load()) + "\n";
  logMsg += "  SPAM_KEY_DOWN_DURATION: " +
            std::to_string(SpamKeyDownDurationMs.load()) + "\n";
  logMsg +=
      "  isLocked: " + std::string(IsLocked.load() ? "true" : "false") + "\n";
  logMsg +=
      "  enable_spam: " + std::string(EnableSpam.load() ? "true" : "false") +
      "\n";
  logMsg += "  enable_snaptap: " +
            std::string(EnableSnapTap.load() ? "true" : "false") + "\n";
  logMsg += "  KEY_SPAM_TRIGGER: " +
            FormatVirtualKeyName(KeySpamTrigger.load()) +
            " (VK: " + std::to_string(KeySpamTrigger.load()) + ")\n";
  logMsg += "  enable_turbo_loot: " +
            std::string(EnableTurboLoot.load() ? "true" : "false") + "\n";
  logMsg += "  turbo_loot_key: " + std::to_string(TurboLootKey.load()) + "\n";
  logMsg +=
      "  turbo_loot_delay: " + std::to_string(TurboLootDelayMs.load()) + "\n";
  logMsg +=
      "  turbo_loot_duration: " + std::to_string(TurboLootDurationMs.load()) +
      "\n";
  logMsg += "  enable_turbo_jump: " +
            std::string(EnableTurboJump.load() ? "true" : "false") + "\n";
  logMsg += "  turbo_jump_key: " + std::to_string(TurboJumpKey.load()) + "\n";
  logMsg +=
      "  turbo_jump_delay: " + std::to_string(TurboJumpDelayMs.load()) + "\n";
  logMsg +=
      "  turbo_jump_duration: " + std::to_string(TurboJumpDurationMs.load());
  Logger::GetInstance().Log(logMsg);
}

void SaveConfig() {
  std::lock_guard<std::mutex> ioLock(g_configIoMutex);
  ValidateConfig();

  // Update-in-place: preserve unknown keys/comments, replace known keys, append
  // missing ones.
  const std::string configPath = GetConfigFilePath();
  std::vector<std::string> lines;
  {
    std::ifstream in(configPath);
    std::string line;
    while (std::getline(in, line)) {
      lines.push_back(line);
    }
  }

  struct Entry {
    const char *key;
    std::string value;
    bool found;
  };
  Entry entries[] = {
      {Keys::kSpamDelayMs,       std::to_string(SpamDelayMs.load()), false},
      {Keys::kSpamKeyDownDuration, std::to_string(SpamKeyDownDurationMs.load()), false},
      {Keys::kIsLocked,          IsLocked.load() ? "true" : "false", false},
      {Keys::kEnableSpam,        EnableSpam.load() ? "true" : "false", false},
      {Keys::kEnableSnapTap,     EnableSnapTap.load() ? "true" : "false", false},
      {Keys::kKeySpamTrigger,    FormatConfigKey(KeySpamTrigger.load()), false},
      {Keys::kKeySpamTriggerMode, std::to_string(static_cast<int>(KeySpamTriggerMode.load())), false},
      {Keys::kEnableTurboLoot,   EnableTurboLoot.load() ? "true" : "false", false},
      {Keys::kTurboLootKey,      FormatConfigKey(TurboLootKey.load()), false},
      {Keys::kTurboLootDelay,    std::to_string(TurboLootDelayMs.load()), false},
      {Keys::kTurboLootDuration, std::to_string(TurboLootDurationMs.load()), false},
      {Keys::kTurboLootMode,     std::to_string(static_cast<int>(TurboLootMode.load())), false},
      {Keys::kEnableTurboJump,   EnableTurboJump.load() ? "true" : "false", false},
      {Keys::kTurboJumpKey,      FormatConfigKey(TurboJumpKey.load()), false},
      {Keys::kTurboJumpDelay,    std::to_string(TurboJumpDelayMs.load()), false},
      {Keys::kTurboJumpDuration, std::to_string(TurboJumpDurationMs.load()), false},
      {Keys::kTurboJumpMode,     std::to_string(static_cast<int>(TurboJumpMode.load())), false},
      {Keys::kInputBackend,      std::to_string(SelectedBackend.load()), false},
      {Keys::kEnableSuperglide,  EnableSuperglide.load() ? "true" : "false", false},
      {Keys::kSuperglideBind,    FormatConfigKey(SuperglideBind.load()), false},
      {Keys::kTargetFps,         std::to_string(TargetFPS.load()), false},
      {Keys::kSuperglideMode,    std::to_string(static_cast<int>(KeybindMode::Hold)), false},
      {Keys::kEnableJitter,      EnableJitter.load() ? "true" : "false", false},
      {Keys::kJitterMs,          std::to_string(JitterMs.load()), false},
      {Keys::kDebounceUs,        std::to_string(DebounceUs.load()), false},
  };

  auto trim = [](std::string &s) {
    size_t first = s.find_first_not_of(" \t\n\r\f\v");
    if (first == std::string::npos) {
      s.clear();
      return;
    }
    size_t last = s.find_last_not_of(" \t\n\r\f\v");
    s = s.substr(first, last - first + 1);
  };

  std::unordered_map<std::string_view, size_t> entryMap;
  const size_t numEntries = sizeof(entries) / sizeof(entries[0]);
  for (size_t i = 0; i < numEntries; ++i) {
    entryMap[entries[i].key] = i;
  }

  for (std::string &line : lines) {
    std::string working = line;
    trim(working);
    if (working.empty() || working[0] == '#') {
      continue;
    }

    size_t eq = working.find('=');
    if (eq == std::string::npos) {
      continue;
    }

    std::string key = working.substr(0, eq);
    trim(key);

    if (key == "isWASDStrafingEnabled") {
      line = "# legacy key migrated to enable_spam";
      continue;
    }

    auto it = entryMap.find(key);
    if (it != entryMap.end()) {
      size_t idx = it->second;
      line = key + " = " + entries[idx].value;
      entries[idx].found = true;
    }
  }

  bool anyMissing = false;
  for (const auto &e : entries) {
    if (!e.found) {
      anyMissing = true;
      break;
    }
  }

  if (lines.empty()) {
    lines.push_back("# Configuration file for StrafeHelper");
  }

  if (anyMissing) {
    lines.push_back("");
    lines.push_back("# Updated by StrafeHelper");
    for (const auto &e : entries) {
      if (!e.found) {
        lines.push_back(std::string(e.key) + " = " + e.value);
      }
    }
  }

  const std::string tempPath = configPath + ".tmp";
  std::ofstream out(tempPath, std::ios::trunc);
  if (!out.is_open()) {
    Logger::GetInstance().Log("Warning: Failed to open config file '" +
                              tempPath + "' for writing.");
    return;
  }

  for (size_t i = 0; i < lines.size(); ++i) {
    out << lines[i];
    if (i + 1 < lines.size())
      out << std::endl;
  }
  out.close();
  if (!out) {
    DeleteFileA(tempPath.c_str());
    Logger::GetInstance().Log("Warning: Failed to write config file '" +
                              tempPath + "'.");
    return;
  }

  if (!MoveFileExA(tempPath.c_str(), configPath.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileA(tempPath.c_str());
    Logger::GetInstance().Log("Warning: Failed to replace config file '" +
                              configPath + "'.");
    return;
  }

  Logger::GetInstance().Log("Configuration saved to " + configPath);
}

} // namespace Config
