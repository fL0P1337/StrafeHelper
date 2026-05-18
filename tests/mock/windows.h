#pragma once

#include <cstdint>
#include <cstddef>

// Typedefs
typedef unsigned long DWORD;
typedef unsigned short WORD;
typedef unsigned int UINT;
typedef long LONG;
typedef char* LPSTR;

#ifndef NULL
#define NULL 0
#endif

#define MAKELANGID(p, s)       ((((WORD  )(s)) << 10) | (WORD  )(p))
#define LANG_NEUTRAL                     0x00
#define SUBLANG_DEFAULT                  0x01
#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define FORMAT_MESSAGE_FROM_SYSTEM     0x00001000
#define FORMAT_MESSAGE_IGNORE_INSERTS  0x00000200

// Mock Windows constants
#define MAX_PATH 260
#define KEYEVENTF_SCANCODE 0x0008
#define KEYEVENTF_EXTENDEDKEY 0x0001
#define KEYEVENTF_KEYUP 0x0002

#define MAPVK_VK_TO_VSC 0
#define MAPVK_VSC_TO_VK 1
#define MAPVK_VK_TO_CHAR 2
#define MAPVK_VSC_TO_VK_EX 3
#define MAPVK_VK_TO_VSC_EX 4

// Virtual Keys
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_CONTROL 0x11
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_SHIFT 0x10
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_MENU 0x12
#define VK_SPACE 0x20
#define VK_TAB 0x09
#define VK_ESCAPE 0x1B
#define VK_RETURN 0x0D
#define VK_BACK 0x08
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_MBUTTON 0x04
#define VK_XBUTTON1 0x05
#define VK_XBUTTON2 0x06

// Mock functions
DWORD GetLastError();
size_t FormatMessageA(DWORD dwFlags, const void* lpSource, DWORD dwMessageId, DWORD dwLanguageId, LPSTR lpBuffer, DWORD nSize, void* Arguments);
void* LocalFree(void* hMem);
DWORD GetModuleFileNameW(void* hModule, wchar_t* lpFilename, DWORD nSize);
UINT MapVirtualKeyW(UINT uCode, UINT uMapType);
int GetKeyNameTextA(LONG lParam, char* lpString, int cchSize);
