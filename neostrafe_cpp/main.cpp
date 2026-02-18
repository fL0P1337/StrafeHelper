// main.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Application.h" // For InitializeApplication
#include "Config.h"      // For APP_NAME, VERSION
#include "Utils.h"       // For LogError
#include <iostream>      // For console output
#include <cstdio>        // For freopen_s, setvbuf
#include <tchar.h>       // For SetConsoleTitle

static void SetupConsole() {
    // If launched from an existing console, reuse it. Otherwise, always create one (even in Release builds).
    if (!GetConsoleWindow()) {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
            if (!AllocConsole()) {
                OutputDebugStringA("ERROR: AllocConsole() failed.\n");
                return;
            }
        }
    }

    FILE* pCout = nullptr;
    FILE* pCerr = nullptr;
    FILE* pCin = nullptr;

    // Redirect standard streams so std::cout/cerr work reliably.
    if (freopen_s(&pCout, "CONOUT$", "w", stdout) == 0 &&
        freopen_s(&pCerr, "CONOUT$", "w", stderr) == 0 &&
        freopen_s(&pCin, "CONIN$", "r", stdin) == 0)
    {
        clearerr(stdout);
        clearerr(stderr);
        clearerr(stdin);

        // Unbuffered output to make logs show immediately.
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);

        const std::string title = std::string(Config::APP_NAME) + " v" + Config::VERSION + " Console";
        SetConsoleTitleA(title.c_str());
        std::ios::sync_with_stdio(true);

        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbi = {};
        if (hOut != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(hOut, &csbi)) {
            const WORD oldAttr = csbi.wAttributes;
            SetConsoleTextAttribute(hOut, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            std::cout << "StrafeHelper console attached." << std::endl;
            SetConsoleTextAttribute(hOut, oldAttr);
        }
        else {
            std::cout << "StrafeHelper console attached." << std::endl;
        }
    }
    else {
        OutputDebugStringA("ERROR: Failed to redirect standard streams to console.\n");
    }
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    SetupConsole();

    // Use Config constants for name/version
    std::cout << Config::APP_NAME << " v" << Config::VERSION << " starting..." << std::endl;

    // Initialize core components
    if (!InitializeApplication(hInstance)) {
        LogError("Application Initialization Failed!");
        // CleanupApplication should have been called partially by InitializeApplication on failure
        MessageBoxA(NULL, "StrafeHelper failed to initialize. See console or log for details.", "Initialization Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    std::cout << "StrafeHelper running. Right-click tray icon to configure or exit." << std::endl;

    // Main message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    std::cout << "Exiting StrafeHelper via message loop end (wParam=" << msg.wParam << ")." << std::endl;

    // Cleanup is handled by WM_DESTROY -> CleanupApplication()
    // Do not call CleanupApplication() here again.

    return (int)msg.wParam;
}

// Some build configs use /SUBSYSTEM:CONSOLE; provide main() so they link cleanly.
int main() {
    return WinMain(GetModuleHandle(nullptr), nullptr, GetCommandLineA(), SW_SHOWDEFAULT);
}
