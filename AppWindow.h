// AppWindow.h
#pragma once

#include <windows.h>

// Callback remains global or static in CPP
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool CreateAppWindow(HINSTANCE hInstance);
void DestroyAppWindow(); // Optional explicit destroy function