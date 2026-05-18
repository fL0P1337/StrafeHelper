// Application.h
#pragma once

#include "Config.h"
#include <windows.h>


bool InitializeApplication(HINSTANCE hInstance);
void CleanupApplication();

// Runtime backend switching (callable from GUI thread).
// Stops the current backend / hook thread and starts the requested backend.
void SwitchBackend(Config::InputBackendKind kind);
