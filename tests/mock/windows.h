#pragma once
#include <iostream>
#include <atomic>
#include <thread>
#include <chrono>

typedef void* HANDLE;
typedef unsigned long DWORD;
typedef int BOOL;
#define NULL 0
#define WINAPI

typedef unsigned short WORD;
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
typedef void* HHOOK;
typedef void* HWND;
typedef void* HINSTANCE;
typedef void* CRITICAL_SECTION;
typedef void* NOTIFYICONDATA;

#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_MBUTTON 0x04
typedef void* LPVOID;

typedef struct tagINPUT {
    DWORD type;
    union {
        struct {
            WORD wVk;
            WORD wScan;
            DWORD dwFlags;
            DWORD time;
            void* dwExtraInfo;
        } ki;
    };
} INPUT;

#define INPUT_KEYBOARD 1

inline unsigned int SendInput(unsigned int cInputs, INPUT *pInputs, int cbSize) { return cInputs; }
inline void SetEvent(HANDLE hEvent) {}
inline DWORD WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds) { return 0; }
inline int CloseHandle(HANDLE hObject) { return 1; }

typedef long long LONGLONG;

typedef union _LARGE_INTEGER {
    struct {
        DWORD LowPart;
        long HighPart;
    };
    struct {
        DWORD LowPart;
        long HighPart;
    } u;
    LONGLONG QuadPart;
} LARGE_INTEGER;

#define INFINITE 0xFFFFFFFF

inline int timeBeginPeriod(unsigned int period) { return 0; }
inline int timeEndPeriod(unsigned int period) { return 0; }
inline int QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency) { lpFrequency->QuadPart = 1000000; return 1; }
inline int QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount) { lpPerformanceCount->QuadPart = 0; return 1; }
inline void Sleep(DWORD dwMilliseconds) {}

// Making CreateThread configurable for tests
extern bool g_mockCreateThreadShouldFail;
inline HANDLE CreateThread(void* lpThreadAttributes, size_t dwStackSize, void* lpStartAddress, void* lpParameter, DWORD dwCreationFlags, DWORD* lpThreadId) {
    if (g_mockCreateThreadShouldFail) {
        return NULL;
    }
    return (HANDLE)1;
}
