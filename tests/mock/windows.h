#pragma once

typedef unsigned long DWORD;
typedef unsigned short WORD;
typedef unsigned int UINT;
typedef long LONG;
typedef char* LPSTR;

#define MAX_PATH 260
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

#define MAPVK_VK_TO_VSC 0
#define MAPVK_VK_TO_VSC_EX 4

#define KEYEVENTF_SCANCODE 0x0008
#define KEYEVENTF_EXTENDEDKEY 0x0001
#define KEYEVENTF_KEYUP 0x0002

#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x00000200
#define LANG_NEUTRAL 0x00
#define SUBLANG_DEFAULT 0x01
#define MAKELANGID(p, s) ((((WORD)(s)) << 10) | (WORD)(p))

#define NULL 0

inline UINT MapVirtualKeyW(UINT uCode, UINT uMapType) { return 0; }
inline int GetKeyNameTextA(LONG lParam, LPSTR lpString, int cchSize) { return 0; }
inline DWORD GetLastError() { return 0; }
inline DWORD FormatMessageA(DWORD dwFlags, const void* lpSource, DWORD dwMessageId, DWORD dwLanguageId, LPSTR lpBuffer, DWORD nSize, void* Arguments) { return 0; }
inline void* LocalFree(void* hMem) { return NULL; }
inline DWORD GetModuleFileNameW(void* hModule, wchar_t* lpFilename, DWORD nSize) { return 0; }
