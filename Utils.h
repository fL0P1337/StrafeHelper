// Utils.h
#pragma once

#include <string>
#include <windows.h> // For DWORD

void LogError(const std::string& message, DWORD errorCode = 0);

/**
 * Returns the absolute path to the directory containing the current executable,
 * including a trailing backslash.
 */
std::wstring GetExecutableDirectory();
