#include "windows.h"
#include <cstring>

DWORD GetLastError() { return 0; }

size_t FormatMessageA(DWORD /*dwFlags*/, const void* /*lpSource*/, DWORD /*dwMessageId*/, DWORD /*dwLanguageId*/, LPSTR /*lpBuffer*/, DWORD /*nSize*/, void* /*Arguments*/) {
    return 0;
}

void* LocalFree(void* /*hMem*/) { return NULL; }

DWORD GetModuleFileNameW(void* /*hModule*/, wchar_t* /*lpFilename*/, DWORD /*nSize*/) {
    return 0;
}

UINT MapVirtualKeyW(UINT uCode, UINT uMapType) {
    if (uMapType == MAPVK_VK_TO_VSC_EX || uMapType == MAPVK_VK_TO_VSC) {
        if (uCode == VK_SPACE) return 0x39;
        if (uCode == 'A') return 0x1E;
        if (uCode == VK_ESCAPE) return 0x01;
        if (uCode == VK_RETURN) return 0x1C;
        if (uCode == VK_RCONTROL) return 0xE01D; // Extended key example
    }
    return 0;
}

int GetKeyNameTextA(LONG /*lParam*/, char* /*lpString*/, int /*cchSize*/) {
    return 0;
}
