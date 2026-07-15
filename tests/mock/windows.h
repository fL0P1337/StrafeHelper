#pragma once
#include <iostream>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <thread>
#include <chrono>

// Basic typedefs
typedef void* HANDLE;
typedef void* LPVOID;
typedef void* HHOOK;
typedef void* HWND;
typedef void* HINSTANCE;
typedef void* HMODULE;
typedef unsigned long DWORD;
typedef unsigned short WORD;
typedef unsigned char BYTE;
typedef unsigned int UINT;
typedef int BOOL;
typedef long LONG;
typedef long LRESULT;
typedef long long LONGLONG;
typedef unsigned long long ULONGLONG;
typedef char* LPSTR;
typedef unsigned long ULONG_PTR;
typedef ULONG_PTR WPARAM;
typedef long LPARAM;
typedef size_t SIZE_T;
typedef DWORD* LPDWORD;

typedef struct _CRITICAL_SECTION {} CRITICAL_SECTION;

#define WINAPI
#define CALLBACK
#ifndef NULL
#define NULL 0
#endif
#define TRUE 1
#define FALSE 0
#define INFINITE 0xFFFFFFFF
#define MAX_PATH 260
#define TEXT(x) x

// Virtual key codes
#define VK_LBUTTON  0x01
#define VK_RBUTTON  0x02
#define VK_MBUTTON  0x04
#define VK_XBUTTON1 0x05
#define VK_XBUTTON2 0x06
#define VK_BACK     0x08
#define VK_TAB      0x09
#define VK_RETURN   0x0D
#define VK_SHIFT    0x10
#define VK_CONTROL  0x11
#define VK_MENU     0x12
#define VK_ESCAPE   0x1B
#define VK_SPACE    0x20
#define VK_LSHIFT   0xA0
#define VK_RSHIFT   0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU    0xA4
#define VK_RMENU    0xA5
#define VK_OEM_3    0xC0

// Input constants
#define INPUT_KEYBOARD 1
#define KEYEVENTF_EXTENDEDKEY 0x0001
#define KEYEVENTF_KEYUP       0x0002
#define KEYEVENTF_UNICODE     0x0004
#define KEYEVENTF_SCANCODE    0x0008

// MapVirtualKey constants
#define MAPVK_VK_TO_VSC    0
#define MAPVK_VSC_TO_VK    1
#define MAPVK_VK_TO_CHAR   2
#define MAPVK_VSC_TO_VK_EX 3
#define MAPVK_VK_TO_VSC_EX 4

// FormatMessage constants
#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define FORMAT_MESSAGE_IGNORE_INSERTS  0x00000200
#define FORMAT_MESSAGE_FROM_SYSTEM     0x00001000
#define LANG_NEUTRAL    0x00
#define SUBLANG_DEFAULT 0x01
#define MAKELANGID(p, s) ((((unsigned short)(s)) << 10) | (unsigned short)(p))

// LARGE_INTEGER
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

// INPUT structure
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

// Mock threading
extern bool g_mockCreateThreadShouldFail;
inline HANDLE CreateThread(void* lpThreadAttributes, size_t dwStackSize,
                           void* lpStartAddress, void* lpParameter,
                           DWORD dwCreationFlags, DWORD* lpThreadId) {
    if (g_mockCreateThreadShouldFail) {
        return NULL;
    }
    return (HANDLE)1;
}
inline DWORD WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds) { return 0; }
inline int CloseHandle(HANDLE hObject) { return 1; }
inline void SetEvent(HANDLE hEvent) {}
inline unsigned int SendInput(unsigned int cInputs, INPUT *pInputs, int cbSize) { return cInputs; }
inline DWORD GetCurrentThreadId() { return 1; }

// Timing
inline int timeBeginPeriod(unsigned int period) { return 0; }
inline int timeEndPeriod(unsigned int period) { return 0; }
inline int QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency) {
    lpFrequency->QuadPart = 10000000; // 10MHz
    return 1;
}
inline int QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount) {
    static const auto start = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count();
    lpPerformanceCount->QuadPart = elapsed / 100; // convert to 10MHz ticks
    return 1;
}
inline void Sleep(DWORD dwMilliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(dwMilliseconds));
}
inline ULONGLONG GetTickCount64() {
    return static_cast<ULONGLONG>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline void YieldProcessor() {
    std::this_thread::yield();
}

// MapVirtualKeyW mock
inline unsigned int MapVirtualKeyW(unsigned int uCode, unsigned int uMapType) {
    if (uMapType == MAPVK_VK_TO_VSC_EX || uMapType == MAPVK_VK_TO_VSC) {
        if (uCode == VK_SPACE) return 0x39;
        if (uCode == VK_LCONTROL) return 0x1D;
        if (uCode == VK_RCONTROL) return 0xE01D;
        if (uCode == 'A') return 0x1E;
        if (uCode == VK_ESCAPE) return 0x01;
        if (uCode == VK_RETURN) return 0x1C;
    }
    return 0;
}

// GetKeyNameTextA mock
inline int GetKeyNameTextA(long lParam, char* lpString, int cchSize) {
    int scanCode = (lParam >> 16) & 0xFF;
    if (scanCode == 0x39) {
        snprintf(lpString, cchSize, "Space");
        return 5;
    }
    return 0;
}

// Error handling mocks
inline DWORD GetLastError() { return 123; }
inline size_t FormatMessageA(DWORD dwFlags, const void* lpSource, DWORD dwMessageId,
                             DWORD dwLanguageId, LPSTR lpBuffer, DWORD nSize, void* Arguments) {
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

// Module/path mocks
inline DWORD GetModuleFileNameW(HINSTANCE hModule, wchar_t* lpFilename, DWORD nSize) {
    const wchar_t* mockPath = L"C:\\Mock\\Path\\StrafeHelper.exe";
    size_t len = wcslen(mockPath);
    if (len < nSize) {
        wcscpy(lpFilename, mockPath);
        return (DWORD)len;
    }
    return 0;
}

// VkKeyScanA mock
inline short VkKeyScanA(char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch;
    if (ch >= 'a' && ch <= 'z') return ch - 32;
    if (ch >= '0' && ch <= '9') return ch;
    return -1;
}

// Critical section stubs
inline void InitializeCriticalSection(CRITICAL_SECTION* cs) {}
inline void DeleteCriticalSection(CRITICAL_SECTION* cs) {}
inline void EnterCriticalSection(CRITICAL_SECTION* cs) {}
inline void LeaveCriticalSection(CRITICAL_SECTION* cs) {}

// File operation mocks
#ifndef MOVEFILE_REPLACE_EXISTING
#define MOVEFILE_REPLACE_EXISTING 0x1
#endif
#ifndef MOVEFILE_WRITE_THROUGH
#define MOVEFILE_WRITE_THROUGH 0x8
#endif
inline BOOL DeleteFileA(const char* path) {
    return std::remove(path) == 0;
}
inline BOOL MoveFileExA(const char* existingPath, const char* newPath,
                        DWORD flags) {
    if ((flags & MOVEFILE_REPLACE_EXISTING) != 0) {
        std::remove(newPath);
    }
    return std::rename(existingPath, newPath) == 0;
}

// Code page constants
#ifndef CP_ACP
#define CP_ACP 0
#endif

// WideCharToMultiByte stub — returns empty narrow path in test context.
// GetExecutableDirectory() returns a well-known mock path; the test stub
// for GetExecutableDirectory() (in stub_utils.cpp) already returns L""
// so GetConfigFilePath() will fall back to CONFIG_FILE_NAME anyway.
inline int WideCharToMultiByte(unsigned int /*CodePage*/, DWORD /*dwFlags*/,
                               const wchar_t* /*lpWideCharStr*/, int /*cchWideChar*/,
                               char* lpMultiByteStr, int cbMultiByte,
                               const char* /*lpDefaultChar*/, int* /*lpUsedDefaultChar*/) {
    if (lpMultiByteStr && cbMultiByte > 0) {
        lpMultiByteStr[0] = '\0';
    }
    return 1; // Indicate success with 1-char (null terminator) result
}
