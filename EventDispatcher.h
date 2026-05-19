// EventDispatcher.h
//
// Translates raw VK key events into feature actions.
//
// Responsibilities:
//   - Tracks physical key state in Globals::g_KeyInfo
//   - Routes each key event to the appropriate feature handler
//     (Spam, SnapTap, TurboLoot, TurboJump, Superglide)
//   - Returns true when the physical key event should be suppressed
//
// This module is intentionally decoupled from Application.cpp so that
// the dispatch logic can be unit-tested in isolation with a mock backend.
#pragma once

// Routes a key event by VK code to the appropriate enabled feature.
// Called from DispatchKeyEvent (keyboard backend callback) and from
// HandleSideMouseButton (raw mouse input path).
// Returns true if the event should be suppressed from the target application.
[[nodiscard]] bool HandleFeatureKeyEvent(int vkCode, bool isKeyDown) noexcept;
