#pragma once
<<<<<<< HEAD
#include <iostream>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <thread>
#include <chrono>

typedef void* HANDLE;
typedef unsigned long DWORD;
typedef unsigned int UINT;
typedef long LONG;
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
#define MAPVK_VK_TO_VSC 0
#define MAPVK_VSC_TO_VK 1
#define MAPVK_VK_TO_CHAR 2
#define MAPVK_VSC_TO_VK_EX 3
#define MAPVK_VK_TO_VSC_EX 4

#define KEYEVENTF_EXTENDEDKEY 0x0001
#define KEYEVENTF_KEYUP 0x0002
#define KEYEVENTF_UNICODE 0x0004
#define KEYEVENTF_SCANCODE 0x0008

inline unsigned int MapVirtualKeyW(unsigned int uCode, unsigned int uMapType) {
    if (uMapType == MAPVK_VK_TO_VSC_EX || uMapType == MAPVK_VK_TO_VSC) {
        // Simple mock mapping for tests
        if (uCode == VK_SPACE) return 0x39;
        if (uCode == VK_LCONTROL) return 0x1D;
        if (uCode == VK_RCONTROL) return 0xE01D; // Extended key
    }
    return 0;
}

inline int GetKeyNameTextA(long lParam, char* lpString, int cchSize) {
    // Simple mock
    int scanCode = (lParam >> 16) & 0xFF;
    if (scanCode == 0x39) {
        snprintf(lpString, cchSize, "Space");
        return 5;
    }
    return 0;
}

#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x00000200
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000

#define LANG_NEUTRAL 0x00
#define SUBLANG_DEFAULT 0x01
#define MAKELANGID(p, s) ((((unsigned short)(s)) << 10) | (unsigned short)(p))

typedef char* LPSTR;

inline DWORD GetLastError() { return 123; } // Mock error code
inline size_t FormatMessageA(DWORD dwFlags, const void* lpSource, DWORD dwMessageId, DWORD dwLanguageId, LPSTR lpBuffer, DWORD nSize, void* Arguments) {
    if (dwFlags & FORMAT_MESSAGE_ALLOCATE_BUFFER) {
        const char* msg = "Mock System Error";
        char* buf = (char*)malloc(strlen(msg) + 1);
        strcpy(buf, msg);
        *(LPSTR*)lpBuffer = buf;
        return strlen(msg);
    }
    return 0;
}
inline void* LocalFree(void* hMem) {
    free(hMem);
    return NULL;
}

#define MAX_PATH 260
inline DWORD GetModuleFileNameW(HINSTANCE hModule, wchar_t* lpFilename, DWORD nSize) {
    const wchar_t* mockPath = L"C:\\Mock\\Path\\StrafeHelper.exe";
    size_t len = wcslen(mockPath);
    if (len < nSize) {
        wcscpy(lpFilename, mockPath);
        return (DWORD)len;
    }
    return 0;
=======
#include <stdint.h>
#include <stddef.h>

typedef void* HANDLE;
typedef void* LPVOID;
typedef unsigned long DWORD;
typedef int BOOL;
typedef unsigned short WORD;
typedef long long LONGLONG;
typedef size_t SIZE_T;
typedef DWORD* LPDWORD;
typedef void* HHOOK;
typedef void* HWND;
typedef void* HINSTANCE;
typedef struct _CRITICAL_SECTION {} CRITICAL_SECTION;

#define WINAPI
#ifndef NULL
#define NULL 0
#endif
#define INFINITE 0xFFFFFFFF
#define TRUE 1
#define FALSE 0

typedef union _LARGE_INTEGER {
  struct {
    DWORD LowPart;
    long HighPart;
  };
  struct {
    DWORD LowPart;
    long HighPart;
  } u;
  long long QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

// Threading
HANDLE CreateThread(LPVOID lpThreadAttributes, SIZE_T dwStackSize, DWORD (WINAPI *lpStartAddress)(LPVOID), LPVOID lpParameter, DWORD dwCreationFlags, LPDWORD lpThreadId);
DWORD WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);
BOOL CloseHandle(HANDLE hObject);
BOOL SetEvent(HANDLE hEvent);
HANDLE CreateEvent(LPVOID lpEventAttributes, BOOL bManualReset, BOOL bInitialState, const char* lpName);
void Sleep(DWORD dwMilliseconds);

// Timing
BOOL QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount);
BOOL QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency);

// Types for inputs
#define VK_SPACE 0x20
#define VK_LCONTROL 0xA2

#define INPUT_KEYBOARD 1
typedef struct tagKEYBDINPUT {
  unsigned short wVk;
  unsigned short wScan;
  DWORD dwFlags;
  DWORD time;
  void* dwExtraInfo;
} KEYBDINPUT;

typedef struct tagINPUT {
  DWORD type;
  union {
    KEYBDINPUT ki;
  };
} INPUT;

#define KEYEVENTF_SCANCODE 0x0008
#define KEYEVENTF_KEYUP 0x0002

DWORD SendInput(DWORD cInputs, INPUT *pInputs, int cbSize);

extern "C" {
  void timeBeginPeriod(DWORD uPeriod);
  void timeEndPeriod(DWORD uPeriod);
>>>>>>> origin/jules-13692354564841379828-b7768257
}
