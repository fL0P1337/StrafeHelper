// SuperglideLogic.h
#pragma once

bool StartSuperglideThread();
void StopSuperglideThread();

void TriggerSuperglide() noexcept;
enum class SuperglideExecutionState : int {
  Idle = 0,
  Executing = 1,
  Cooldown = 2,
};

SuperglideExecutionState GetSuperglideExecutionState() noexcept;
unsigned long GetSuperglideCooldownRemainingMs() noexcept;
