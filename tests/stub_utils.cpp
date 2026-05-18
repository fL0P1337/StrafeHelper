#include "Utils.h"
#include <string>
#include <iostream>

void LogError(const std::string& message, DWORD errorCode) {
    std::cout << "Mock LogError: " << message << std::endl;
}

DWORD ApplyJitter(DWORD baseDelay) { return baseDelay; }
WORD VirtualKeyToScanCode(int vk) { return 0; }
DWORD VirtualKeyInputFlags(int vk, bool keyDown) { return 0; }
std::string FormatVirtualKeyName(int vk) { return "MockKey"; }
std::wstring GetExecutableDirectory() { return L""; }
