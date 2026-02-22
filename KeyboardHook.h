// KeyboardHook.h
#pragma once

#include <windows.h>

// The callback function remains global or static within the CPP
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);

bool SetupKeyboardHook(HINSTANCE hInstance);
void TeardownKeyboardHook();

// Called from UI (tray toggle) to immediately apply/disable SnapTap behavior.
void OnSnapTapToggled(bool enabled);

// Called when runtime configuration changes require output state to be re-applied.
void RefreshMovementState();
