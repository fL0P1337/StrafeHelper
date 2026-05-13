// Utils.h
#pragma once

#include <string>
#include <windows.h> // For DWORD

DWORD ApplyJitter(DWORD baseDelay);
void LogError(const std::string& message, DWORD errorCode = 0);
WORD VirtualKeyToScanCode(int vk);
DWORD VirtualKeyInputFlags(int vk, bool keyDown);
std::string FormatVirtualKeyName(int vk);

/**
 * Returns the absolute path to the directory containing the current executable,
 * including a trailing backslash.
 */
std::wstring GetExecutableDirectory();
