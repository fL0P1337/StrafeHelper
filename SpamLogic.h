// SpamLogic.h
#pragma once

#include <cstddef>

// Injects key-down or key-up events through the active input backend.
// `count` must be <= 4 (WASD bound).
void SendKeyInputBatch(const int *keys, std::size_t count, bool keyDown);
void CleanupSpamState(bool restoreHeldKeys);

bool StartSpamThread();
void StopSpamThread();

void TriggerSpamEvent() noexcept;