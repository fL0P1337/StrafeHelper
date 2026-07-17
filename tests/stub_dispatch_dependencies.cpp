#include "../MovementStateManager.h"

void OnSpamActivated(bool) {}
void OnSpamDeactivated(bool) {}
bool HandleMovementKeyState(int, bool, bool, bool) { return false; }
void TriggerSpamEvent() noexcept {}
void TriggerTurboLoot() noexcept {}
void TriggerTurboJump() noexcept {}