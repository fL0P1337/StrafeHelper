// TurboLogic.h
#pragma once

bool StartTurboLootThread();
void StopTurboLootThread();

bool StartTurboJumpThread();
void StopTurboJumpThread();

void TriggerTurboLoot() noexcept;
void TriggerTurboJump() noexcept;
