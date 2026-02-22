// SpamLogic.h
#pragma once

#include <windows.h>
#include <vector>

// Thread function remains global or static within CPP
DWORD WINAPI SpamThread(LPVOID lpParam);

void SendKeyInputBatch(const std::vector<int>& keys, bool keyDown);
void CleanupSpamState(bool restoreHeldKeys);

bool StartSpamThread();
void StopSpamThread(); // Signals thread to stop and cleans up handle