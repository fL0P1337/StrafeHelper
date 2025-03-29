// KeyboardHook.h
#pragma once

#include <windows.h>

// The callback function remains global or static within the CPP
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

bool SetupKeyboardHook(HINSTANCE hInstance);
void TeardownKeyboardHook();