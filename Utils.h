// Utils.h
#pragma once

#include <string>
#include <windows.h> // For DWORD

void LogError(const std::string& message, DWORD errorCode = 0);

DWORD ApplyJitter(DWORD baseDelay);