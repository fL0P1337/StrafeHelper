#pragma once

#include <cstdint>
#include <string>

enum class InputBackendKind : uint8_t {
  Interception = 0,
  KeyboardHook = 1,
  RawInput = 2,
};

struct RuntimeConfig {
  bool enabled = true;
  bool snaptapEnabled = true;
  uint16_t triggerScanCode = 0x2Eu; // C
  uint32_t spamDownUs = 500;
  uint32_t spamUpUs = 500;
  uint16_t forwardScanCode = 0x11; // W
  uint16_t leftScanCode = 0x1E;    // A
  uint16_t backScanCode = 0x1F;    // S
  uint16_t rightScanCode = 0x20;   // D
  bool isLocked = false;
  InputBackendKind inputBackend = InputBackendKind::Interception;
  bool hasInputBackend = false;
};

namespace Config {
[[nodiscard]] bool Load(RuntimeConfig &out,
                        const std::wstring &path = L"config.cfg") noexcept;
[[nodiscard]] bool Save(const RuntimeConfig &cfg,
                        const std::wstring &path = L"config.cfg") noexcept;
[[nodiscard]] const char *BackendKindToString(InputBackendKind kind) noexcept;
[[nodiscard]] bool BackendKindFromString(const std::string &text,
                                         InputBackendKind &out) noexcept;
} // namespace Config
