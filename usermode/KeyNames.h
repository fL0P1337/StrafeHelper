#pragma once

#include <cstdint>
#include <string>

namespace KeyNames {
[[nodiscard]] uint16_t KeyNameToScancode(const std::string &name) noexcept;
[[nodiscard]] std::string ScancodeToKeyName(uint16_t scanCode);
} // namespace KeyNames
