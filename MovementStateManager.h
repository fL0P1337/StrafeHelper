// MovementStateManager.h
#pragma once

#include <windows.h>

// Called from UI (tray toggle) to immediately apply/disable SnapTap behavior.
void OnSnapTapToggled(bool enabled);

void OnSpamActivated(bool snapTapEnabled);
void OnSpamDeactivated(bool snapTapEnabled);

// isKeyUp has been removed — it was always !isKeyDown at every call site.
bool HandleMovementKeyState(int vkCode, bool isKeyDown,
                            bool spamActive, bool snapTapEnabled);

// Called when runtime configuration changes require output state to be re-applied.
void RefreshMovementState();
