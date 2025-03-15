#pragma once
#include <windows.h>
#include <shellapi.h>
#include <map>
#include <string>
#include <iostream>


// Constants for tray icon and menu
#define ID_TRAY_APP_ICON                1001
#define ID_TRAY_EXIT_CONTEXT_MENU_ITEM  3000
#define ID_TRAY_LOCK_FUNCTION           3001
#define WM_TRAYICON                     (WM_USER + 1)

// Structure to track key states
struct KeyState {
    bool physicalKeyDown = false;  // Is the key physically pressed?
    bool spamActive = false;       // Is the key being spammed?
};

// Global variables
HHOOK hHook = NULL;
NOTIFYICONDATA nid;
bool isLocked = false;          // Toggle for enabling/disabling the feature
bool isCtrlSpamActive = false;  // Track Left Ctrl spamming state for WASD
std::map<int, KeyState> KeyInfo; // Track state of WASD keys
const int SPAM_DELAY_MS = 1;    // Delay between spams in milliseconds

// Function declarations
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void InitNotifyIconData(HWND hwnd);
void SendKey(int target, bool keyDown);
DWORD WINAPI SpamThread(LPVOID lpParam);
