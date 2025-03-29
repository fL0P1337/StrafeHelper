// main.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "Application.h" // For InitializeApplication
#include "Config.h"      // For APP_NAME, VERSION
#include "Utils.h"       // For LogError
#include <iostream>      // For console output
#include <tchar.h>       // For SetConsoleTitle

#ifdef _DEBUG
void SetupDebugConsole() {
    if (!GetConsoleWindow()) {
        if (AllocConsole()) {
            FILE* pCout, * pCerr, * pCin;
            // Redirect standard streams
            if (freopen_s(&pCout, "CONOUT$", "w", stdout) == 0 &&
                freopen_s(&pCerr, "CONOUT$", "w", stderr) == 0 &&
                freopen_s(&pCin, "CONIN$", "r", stdin) == 0)
            {
                // Clear error state flags potentially set by failed freopen_s
                clearerr(stdout);
                clearerr(stderr);
                clearerr(stdin);

                // Set console title using TCHAR
                SetConsoleTitle(TEXT("StrafeHelper Debug Console"));

                // Sync std::cout/cerr with C stdio after redirection (optional but good practice)
                std::ios::sync_with_stdio(true);

                std::cout << "Debug Console Allocated." << std::endl;
            }
            else {
                // Handle freopen_s failure (e.g., log to debugger output)
                OutputDebugStringA("ERROR: Failed to redirect standard streams to console.\n");
            }
        }
        else {
            OutputDebugStringA("ERROR: AllocConsole() failed.\n");
        }
    }
    else {
        std::cout << "Using existing console." << std::endl;
    }
}
#endif // _DEBUG


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {

#ifdef _DEBUG
    SetupDebugConsole();
#endif

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