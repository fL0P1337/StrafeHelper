// Application.h
#pragma once

#include "Config.h"
#include "InputBackend.h"
#include <windows.h>

bool InitializeApplication(HINSTANCE hInstance);
void CleanupApplication();

void HandleSideMouseButton(int vkCode, bool isDown);

// Runtime backend switching (callable from GUI thread).
// Stops the current backend / hook thread and starts the requested backend.
bool SwitchBackend(Config::InputBackendKind kind);

bool DispatchKeyEvent(const NEO_KEY_EVENT &evt) noexcept;
bool GetActiveBackendStatus(BackendStatus &out) noexcept;
bool InjectKey(int vk, bool keyDown) noexcept;
