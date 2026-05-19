// SpamLogic.h
#pragma once

#include <vector>

void SendKeyInputBatch(const std::vector<int>& keys, bool keyDown);
void CleanupSpamState(bool restoreHeldKeys);

bool StartSpamThread();
void StopSpamThread();

void TriggerSpamEvent() noexcept;