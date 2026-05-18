#pragma once

typedef unsigned long DWORD;
typedef unsigned short WORD;
typedef long LONG;
typedef unsigned int UINT;
typedef char* LPSTR;

#define TEXT(x) x

#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_CONTROL 0x11
#define VK_SHIFT 0x10
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_MENU 0x12
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_SPACE 0x20
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_ESCAPE 0x1B
#define VK_BACK 0x08
#define VK_XBUTTON1 0x05
#define VK_XBUTTON2 0x06

// Mocking some other things needed for compilation
void Sleep(DWORD dwMilliseconds);
