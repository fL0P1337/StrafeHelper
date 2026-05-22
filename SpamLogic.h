// SpamLogic.h
#pragma once

#include <cstddef>

// Injects key-down or key-up events for the given VK codes in one SendInput
// call. `count` must be <= 4 (WASD bound); buffer is stack-allocated.
void SendKeyInputBatch(const int *keys, std::size_t count, bool keyDown);
void CleanupSpamState(bool restoreHeldKeys);

bool StartSpamThread();
void StopSpamThread();

void TriggerSpamEvent() noexcept;