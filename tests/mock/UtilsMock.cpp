#include "Utils.h"
#include <string>

std::string FormatVirtualKeyName(int vk) {
    if (vk == 0x20) return "Space";
    return std::to_string(vk);
}

void LogError(const std::string& msg, unsigned long errorCode) {}
std::wstring GetExecutableDirectory() { return L"."; }
unsigned long VirtualKeyInputFlags(int vk, bool extended) { return 0; }
unsigned short VirtualKeyToScanCode(int vk) { return 0; }
void ApplyJitter(int baseDelayMs) {}
