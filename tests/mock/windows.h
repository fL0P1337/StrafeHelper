#pragma once
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
}
