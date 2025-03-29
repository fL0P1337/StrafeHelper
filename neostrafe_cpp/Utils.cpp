// Utils.cpp
#include "Utils.h"
#include <iostream>
#include <windows.h> // For FormatMessage, LocalFree

void LogError(const std::string& message, DWORD errorCode) {
    std::cerr << "ERROR: " << message;
    if (errorCode == 0) {
        errorCode = GetLastError(); // Get error code if not provided
    }
    if (errorCode != 0) {
        LPSTR messageBuffer = nullptr;
        size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL, errorCode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);

        if (size > 0 && messageBuffer != nullptr) {
            std::string systemMessage(messageBuffer, size);
            while (!systemMessage.empty() && (systemMessage.back() == '\n' || systemMessage.back() == '\r')) {
                systemMessage.pop_back();
            }
            std::cerr << " (Code: " << errorCode << " - " << systemMessage << ")";
        }
        else {
            std::cerr << " (Code: " << errorCode << " - FormatMessage failed with code " << GetLastError() << ")";
        }
        LocalFree(messageBuffer);
    }
    std::cerr << std::endl;

#ifndef _DEBUG
    // Optional: Show message box in Release builds
    // std::string fullMsg = message + " (Error Code: " + std::to_string(errorCode) + ")";
    // MessageBoxA(NULL, fullMsg.c_str(), "StrafeHelper Error", MB_OK | MB_ICONERROR);
#endif
}