// Config.cpp
#include "Config.h"
#include "Logger.h"
#include <fstream>
#include <string>
#include <vector>
#include <windows.h> // For VkKeyScanA

namespace Config {
// --- Definitions of extern variables from Config.h ---
const char APP_NAME[] = "StrafeHelper";
const char VERSION[] = "1.2_modular"; // Updated version
const char CONFIG_FILE_NAME[] = "config.cfg";

const TCHAR WINDOW_CLASS_NAME[] = TEXT("StrafeHelperWindowClass");
const TCHAR WINDOW_TITLE[] = TEXT("StrafeHelper Hidden Window");

// Initialize atomics with defaults
std::atomic<int> SpamDelayMs{10};
std::atomic<int> SpamKeyDownDurationMs{5};
std::atomic<bool> IsLocked{false};
std::atomic<bool> EnableSpam{true};
std::atomic<bool> EnableSnapTap{true};
std::atomic<int> KeySpamTrigger{'C'}; // VK_KEY 'C'

// Turbo Loot
std::atomic<bool> EnableTurboLoot{false};
std::atomic<int> TurboLootKey{0x45}; // 'E'
std::atomic<int> TurboLootDelayMs{15};
std::atomic<int> TurboLootDurationMs{5};

// Turbo Jump
std::atomic<bool> EnableTurboJump{false};
std::atomic<int> TurboJumpKey{0x20}; // VK_SPACE
std::atomic<int> TurboJumpDelayMs{15};
std::atomic<int> TurboJumpDurationMs{5};

// Input backend (0 = KbdHook default)
std::atomic<int> SelectedBackend{0};

// --- Implementation of LoadConfig ---
void LoadConfig() {
  std::ifstream configFile(CONFIG_FILE_NAME);
  if (!configFile.is_open()) {
    std::string logMsg = "Config file '" + std::string(CONFIG_FILE_NAME) +
                         "' not found. Using defaults.\n";
    logMsg += " Defaults: Delay=" + std::to_string(SpamDelayMs.load()) +
              "ms, Duration=" + std::to_string(SpamKeyDownDurationMs.load()) +
              "ms, Trigger=" + static_cast<char>(KeySpamTrigger.load()) +
              ", Spam=" + (EnableSpam.load() ? "true" : "false") +
              ", SnapTap=" + (EnableSnapTap.load() ? "true" : "false");
    Logger::GetInstance().Log(logMsg);
    return;
  }

  Logger::GetInstance().Log("Loading configuration from " +
                            std::string(CONFIG_FILE_NAME) + "...");
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
      if (key == "SPAM_DELAY_MS")
        SpamDelayMs = std::stoi(value);
      else if (key == "SPAM_KEY_DOWN_DURATION")
        SpamKeyDownDurationMs = std::stoi(value);
      else if (key == "isLocked")
        IsLocked = (value == "true" || value == "1");
      else if (key == "enable_spam") {
        EnableSpam = (value == "true" || value == "1");
        hasEnableSpam = true;
      } else if (key == "isWASDStrafingEnabled")
        legacyEnableSpam = (value == "true" || value == "1");
      else if (key == "enable_snaptap")
        EnableSnapTap = (value == "true" || value == "1");
      else if (key == "KEY_SPAM_TRIGGER") {
        if (!value.empty()) {
          SHORT vkScanResult = VkKeyScanA(value[0]);
          if (vkScanResult != -1) {
            KeySpamTrigger = LOBYTE(vkScanResult);
          } else {
            KeySpamTrigger = static_cast<int>(toupper(value[0]));
            Logger::GetInstance().Log(
                "Warning: Could not map config key '" +
                std::string(1, value[0]) +
                "' via VkKeyScanA. Using direct char value.");
          }
        }
      }
      // Turbo Loot
      else if (key == "enable_turbo_loot")
        EnableTurboLoot = (value == "true" || value == "1");
      else if (key == "turbo_loot_key")
        TurboLootKey = std::stoi(value);
      else if (key == "turbo_loot_delay")
        TurboLootDelayMs = std::stoi(value);
      else if (key == "turbo_loot_duration")
        TurboLootDurationMs = std::stoi(value);
      // Turbo Jump
      else if (key == "enable_turbo_jump")
        EnableTurboJump = (value == "true" || value == "1");
      else if (key == "turbo_jump_key")
        TurboJumpKey = std::stoi(value);
      else if (key == "turbo_jump_delay")
        TurboJumpDelayMs = std::stoi(value);
      else if (key == "turbo_jump_duration")
        TurboJumpDurationMs = std::stoi(value);
      else if (key == "input_backend") {
        int v = std::stoi(value);
        // Clamp to valid range; unknown values default to KbdHook
        SelectedBackend = (v == 1) ? 1 : 0;
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
            std::string(1, static_cast<char>(KeySpamTrigger.load())) +
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
  const std::string spamDelay = std::to_string(SpamDelayMs.load());
  const std::string spamDuration = std::to_string(SpamKeyDownDurationMs.load());
  const std::string isLockedStr = IsLocked.load() ? "true" : "false";
  const std::string enableSpamStr = EnableSpam.load() ? "true" : "false";
  const std::string snapTapStr = EnableSnapTap.load() ? "true" : "false";
  const std::string triggerStr(1, static_cast<char>(KeySpamTrigger.load()));
  const std::string turboLootStr = EnableTurboLoot.load() ? "true" : "false";
  const std::string turboLootKeyStr = std::to_string(TurboLootKey.load());
  const std::string turboLootDelayStr = std::to_string(TurboLootDelayMs.load());
  const std::string turboLootDurStr =
      std::to_string(TurboLootDurationMs.load());
  const std::string turboJumpStr = EnableTurboJump.load() ? "true" : "false";
  const std::string turboJumpKeyStr = std::to_string(TurboJumpKey.load());
  const std::string turboJumpDelayStr = std::to_string(TurboJumpDelayMs.load());
  const std::string turboJumpDurStr =
      std::to_string(TurboJumpDurationMs.load());

  // Update-in-place: preserve unknown keys/comments, replace known keys, append
  // missing ones.
  std::vector<std::string> lines;
  {
    std::ifstream in(CONFIG_FILE_NAME);
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
      {"SPAM_DELAY_MS", spamDelay, false},
      {"SPAM_KEY_DOWN_DURATION", spamDuration, false},
      {"isLocked", isLockedStr, false},
      {"enable_spam", enableSpamStr, false},
      {"enable_snaptap", snapTapStr, false},
      {"KEY_SPAM_TRIGGER", triggerStr, false},
      {"enable_turbo_loot", turboLootStr, false},
      {"turbo_loot_key", turboLootKeyStr, false},
      {"turbo_loot_delay", turboLootDelayStr, false},
      {"turbo_loot_duration", turboLootDurStr, false},
      {"enable_turbo_jump", turboJumpStr, false},
      {"turbo_jump_key", turboJumpKeyStr, false},
      {"turbo_jump_delay", turboJumpDelayStr, false},
      {"turbo_jump_duration", turboJumpDurStr, false},
      {"input_backend", std::to_string(SelectedBackend.load()), false},
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

    for (auto &e : entries) {
      if (key == e.key) {
        line = key + " = " + e.value;
        e.found = true;
        break;
      }
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

  std::ofstream out(CONFIG_FILE_NAME, std::ios::trunc);
  if (!out.is_open()) {
    Logger::GetInstance().Log("Warning: Failed to open config file '" +
                              std::string(CONFIG_FILE_NAME) + "' for writing.");
    return;
  }

  for (size_t i = 0; i < lines.size(); ++i) {
    out << lines[i];
    if (i + 1 < lines.size())
      out << std::endl;
  }
  out.close();

  Logger::GetInstance().Log("Configuration saved to " +
                            std::string(CONFIG_FILE_NAME));
}

} // namespace Config
