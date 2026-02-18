#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "Engine.h"

#include <algorithm>
#include <chrono>
#include <limits>

#include <windows.h>

namespace {
constexpr uint32_t kEventDrainMax = 64;
constexpr auto kWatchdogPeriod = std::chrono::milliseconds(50);
constexpr int kMaxInjectRetries = 3;

[[nodiscard]] bool IsValidKeyIndex(uint16_t sc) noexcept { return sc < 256u; }
} // namespace

Engine::Engine(InputBackend &backend, const RuntimeConfig &config) noexcept
    : backend_(backend) {
  ApplyConfig(config);
}

// ---------------------------------------------------------------------------
// Main event loop — single-threaded, all processing + emission under lock
// ---------------------------------------------------------------------------
void Engine::Run(std::atomic<bool> &running) noexcept {
  NEO_KEY_EVENT events[kEventDrainMax]{};
  std::vector<NEO_KEY_EVENT> batch;
  batch.reserve(kEventDrainMax);
  std::vector<uint8_t> swallow;
  swallow.reserve(kEventDrainMax);
  std::vector<VirtualAction> actions;
  actions.reserve(kEventDrainMax);

  auto nextWatchdog = std::chrono::steady_clock::now() + kWatchdogPeriod;

  while (running.load(std::memory_order_relaxed)) {
    // Determine wait timeout: short when spam is active, else watchdog period.
    uint32_t waitMs;
    {
      std::lock_guard<std::mutex> lock(stateMutex_);
      if (ownershipActive_ && HasActiveDirectionLocked()) {
        waitMs = 0; // spam active — non-blocking poll
      } else {
        waitMs = static_cast<uint32_t>(kWatchdogPeriod.count());
      }
    }

    if (waitMs > 0) {
      backend_.WaitForData(waitMs);
    }
    if (!running.load(std::memory_order_relaxed)) {
      break;
    }

    // Drain all pending events into batch.
    batch.clear();
    for (;;) {
      const uint32_t count = backend_.PollEvents(events, kEventDrainMax);
      if (count == 0) {
        break;
      }
      batch.insert(batch.end(), events, events + count);
    }

    {
      std::lock_guard<std::mutex> lock(stateMutex_);

      // --- Process hardware events ---
      if (!batch.empty()) {
        swallow.assign(batch.size(), 0u);
        actions.clear();

        const Directions dirs = LoadDirections();
        ProcessInputBatchLocked(batch, swallow, actions);

        // Pass through non-swallowed events and track emitted state.
        PassThroughAndTrackLocked(batch, swallow, dirs);

        // Emit synthetic actions (ownership diff) and update emittedDown.
        EmitActionsLocked(actions);
      }

      // --- Spam toggle ---
      if (ownershipActive_ && HasActiveDirectionLocked()) {
        const int64_t nowQpc = timer_.NowTicks();
        if (nowQpc >= nextSpamToggleQpc_) {
          spamPhaseDown_ = !spamPhaseDown_;

          const uint32_t periodUs = spamPhaseDown_
                                        ? std::max<uint32_t>(1u, SpamDownUs())
                                        : std::max<uint32_t>(1u, SpamUpUs());
          nextSpamToggleQpc_ = nowQpc + timer_.UsToTicks(periodUs);

          actions.clear();
          ApplyMovementDiffLocked(LoadDirections(), actions);
          EmitActionsLocked(actions);
        }
      }

      // --- Watchdog ---
      const auto now = std::chrono::steady_clock::now();
      if (now >= nextWatchdog) {
        while (nextWatchdog <= now) {
          nextWatchdog += kWatchdogPeriod;
        }
        actions.clear();
        RunWatchdogLocked(actions);
        EmitActionsLocked(actions);
      }
    } // release stateMutex_

    // Yield briefly when spam is active to avoid 100% CPU spin.
    {
      std::lock_guard<std::mutex> lock(stateMutex_);
      if (ownershipActive_ && HasActiveDirectionLocked()) {
        const int64_t nowQpc = timer_.NowTicks();
        const int64_t remainQpc = nextSpamToggleQpc_ - nowQpc;
        if (remainQpc > 0) {
          const int64_t remainUs = timer_.TicksToUs(remainQpc);
          // Release lock for the wait.
          stateMutex_.unlock();
          if (remainUs > 2000) {
            // Coarse sleep then spin for the tail.
            timer_.PreciseWaitUs(remainUs);
          } else if (remainUs > 0) {
            timer_.SpinWaitUs(remainUs);
          }
          stateMutex_.lock();
        }
      }
    }
  }

  ForceReleaseAllVirtual();
}

// ---------------------------------------------------------------------------
// SetEnabled — called from tray thread
// ---------------------------------------------------------------------------
void Engine::SetEnabled(bool enabled) noexcept {
  enabled_.store(enabled, std::memory_order_relaxed);

  const Directions dirs = LoadDirections();
  std::vector<VirtualAction> actions;

  {
    std::lock_guard<std::mutex> lock(stateMutex_);
    const bool oldOwn = ownershipActive_;

    const bool shouldOwn = triggerHeld_ &&
                           enabled_.load(std::memory_order_relaxed) &&
                           !locked_.load(std::memory_order_relaxed);

    if (oldOwn && !shouldOwn) {
      // OWNED → PASSTHROUGH: reconcile to physical.
      ownershipActive_ = false;
      spamPhaseDown_ = false;
      UpdateActiveAxesLocked(dirs);
      ReconcileToPhysicalLocked(dirs, actions);
      EmitActionsLocked(actions);
      return;
    }

    if (!oldOwn && shouldOwn) {
      // PASSTHROUGH → OWNED: take ownership.
      ownershipActive_ = true;
      spamPhaseDown_ = true;
      nextSpamToggleQpc_ = timer_.NowTicks();
      UpdateActiveAxesLocked(dirs);
      ApplyMovementDiffLocked(dirs, actions);
      EmitActionsLocked(actions);
      return;
    }

    ownershipActive_ = shouldOwn;
    UpdateActiveAxesLocked(dirs);
    if (shouldOwn) {
      ApplyMovementDiffLocked(dirs, actions);
    }
    EmitActionsLocked(actions);
  }
}

bool Engine::IsEnabled() const noexcept {
  return enabled_.load(std::memory_order_relaxed);
}

bool Engine::ToggleEnabled() noexcept {
  const bool next = !enabled_.load(std::memory_order_relaxed);
  SetEnabled(next);
  return next;
}

// ---------------------------------------------------------------------------
// ApplyConfig — called from tray thread
// ---------------------------------------------------------------------------
void Engine::ApplyConfig(const RuntimeConfig &config) noexcept {
  enabled_.store(config.enabled, std::memory_order_relaxed);
  snaptapEnabled_.store(config.snaptapEnabled, std::memory_order_relaxed);
  locked_.store(config.isLocked, std::memory_order_relaxed);
  triggerScanCode_.store(config.triggerScanCode, std::memory_order_relaxed);
  spamDownUs_.store((config.spamDownUs == 0) ? 1u : config.spamDownUs,
                    std::memory_order_relaxed);
  spamUpUs_.store((config.spamUpUs == 0) ? 1u : config.spamUpUs,
                  std::memory_order_relaxed);
  directionScancodes_[0].store(config.forwardScanCode,
                               std::memory_order_relaxed);
  directionScancodes_[1].store(config.leftScanCode, std::memory_order_relaxed);
  directionScancodes_[2].store(config.backScanCode, std::memory_order_relaxed);
  directionScancodes_[3].store(config.rightScanCode, std::memory_order_relaxed);

  const Directions dirs = LoadDirections();
  std::vector<VirtualAction> actions;

  {
    std::lock_guard<std::mutex> lock(stateMutex_);
    const bool oldOwn = ownershipActive_;

    const bool shouldOwn = triggerHeld_ &&
                           enabled_.load(std::memory_order_relaxed) &&
                           !locked_.load(std::memory_order_relaxed);

    if (oldOwn && !shouldOwn) {
      ownershipActive_ = false;
      spamPhaseDown_ = false;
      UpdateActiveAxesLocked(dirs);
      ReconcileToPhysicalLocked(dirs, actions);
    } else if (!oldOwn && shouldOwn) {
      ownershipActive_ = true;
      spamPhaseDown_ = true;
      nextSpamToggleQpc_ = timer_.NowTicks();
      UpdateActiveAxesLocked(dirs);
      ApplyMovementDiffLocked(dirs, actions);
    } else {
      ownershipActive_ = shouldOwn;
      UpdateActiveAxesLocked(dirs);
      if (shouldOwn) {
        ApplyMovementDiffLocked(dirs, actions);
      }
    }

    EmitActionsLocked(actions);
  }
}

uint16_t Engine::TriggerScanCode() const noexcept {
  return triggerScanCode_.load(std::memory_order_relaxed);
}

uint32_t Engine::SpamDownUs() const noexcept {
  return spamDownUs_.load(std::memory_order_relaxed);
}

uint32_t Engine::SpamUpUs() const noexcept {
  return spamUpUs_.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// ProcessInputBatchLocked — update physical state, axes, ownership, swallow
// Must hold stateMutex_.
// ---------------------------------------------------------------------------
void Engine::ProcessInputBatchLocked(
    const std::vector<NEO_KEY_EVENT> &batch, std::vector<uint8_t> &swallow,
    std::vector<VirtualAction> &actions) noexcept {
  if (batch.empty()) {
    return;
  }

  const Directions dirs = LoadDirections();
  const bool canSuppress = backend_.CanSuppressPhysical();
  const bool enabled = enabled_.load(std::memory_order_relaxed);
  const bool locked = locked_.load(std::memory_order_relaxed);
  const bool snaptap = snaptapEnabled_.load(std::memory_order_relaxed);
  const uint16_t triggerSc = triggerScanCode_.load(std::memory_order_relaxed);

  if (swallow.size() != batch.size()) {
    swallow.assign(batch.size(), 0u);
  }

  const bool triggerHeldStart = triggerHeld_;

  // Phase A: Update physical state for entire batch.
  for (size_t i = 0; i < batch.size(); ++i) {
    const NEO_KEY_EVENT &evt = batch[i];

    // Safety net: discard any leftover synthetic events (should not happen
    // now that InjectKey uses SendInput, but kept as defensive measure).
    if (IsSyntheticEvent(evt)) {
      swallow[i] = 1u;
      continue;
    }

    const uint16_t sc = evt.scanCode;
    const bool isDown = IsKeyDown(evt);

    if (IsValidKeyIndex(sc)) {
      const bool oldPhysical = keyStates_[sc].physicalDown;
      if (oldPhysical != isDown) {
        keyStates_[sc].physicalDown = isDown;
      }
      if (!oldPhysical && isDown) {
        keyStates_[sc].lastPressSeq = ++pressSeqCounter_;
      }
    }

    // Track trigger transitions.
    if (sc == triggerSc && triggerHeld_ != isDown) {
      triggerHeld_ = isDown;
    }
  }

  // Phase B: Resolve axes after physical updates.
  if (snaptap) {
    UpdateActiveAxesLocked(dirs);
  }

  // Phase C: Update ownership — handle transitions.
  const bool shouldOwn = triggerHeld_ && enabled && !locked;
  if (ownershipActive_ != shouldOwn) {
    const bool wasOwned = ownershipActive_;
    ownershipActive_ = shouldOwn;

    if (wasOwned && !shouldOwn) {
      // OWNED → PASSTHROUGH: release all engine-held keys to match physical.
      spamPhaseDown_ = false;
      ReconcileToPhysicalLocked(dirs, actions);
    }
    if (!wasOwned && shouldOwn) {
      // PASSTHROUGH → OWNED: begin ownership, start with down phase.
      spamPhaseDown_ = true;
      nextSpamToggleQpc_ = timer_.NowTicks();
    }
  }

  // Phase D: Compute swallow per event using per-event trigger simulation.
  bool triggerSim = triggerHeldStart;
  for (size_t i = 0; i < batch.size(); ++i) {
    const NEO_KEY_EVENT &evt = batch[i];
    if (IsSyntheticEvent(evt)) {
      // Already marked swallow in Phase A.
      continue;
    }

    const uint16_t sc = evt.scanCode;
    const bool isDown = IsKeyDown(evt);
    const bool isTrigger = (sc == triggerSc);
    if (isTrigger && triggerSim != isDown) {
      triggerSim = isDown;
    }

    const bool ownSim = triggerSim && enabled && !locked;
    const bool isMovement = IsMovementKey(sc, dirs);
    // Swallow movement keys when ownership is active OR when SnapTap is
    // enabled (SnapTap drives movement synthetically even without trigger).
    const bool sw = isMovement && canSuppress && (ownSim || snaptap);
    swallow[i] = sw ? 1u : 0u;
  }

  // Phase E: Compute diff for movement keys.
  // When owned: SnapTap + spam logic applies.
  // When not owned but SnapTap enabled: axis resolution applies (no spam).
  // When not owned and no SnapTap: passthrough handles it (no diff needed).
  if (ownershipActive_ || snaptap) {
    ApplyMovementDiffLocked(dirs, actions);
  }
}

// ---------------------------------------------------------------------------
// ApplyMovementDiffLocked — generate synthetic actions for movement keys.
// Called when ownership is active OR SnapTap is enabled.
// ---------------------------------------------------------------------------
void Engine::ApplyMovementDiffLocked(
    const Directions &dirs, std::vector<VirtualAction> &actions) noexcept {
  const std::array<uint16_t, 4> keys{
      {dirs.forward, dirs.left, dirs.back, dirs.right}};

  for (const uint16_t sc : keys) {
    if (!IsValidKeyIndex(sc)) {
      continue;
    }

    const bool desired = DesiredStateLocked(sc);
    KeyState &state = keyStates_[sc];
    if (state.emittedDown == desired) {
      continue;
    }

    actions.emplace_back(sc, desired);
  }
}

// ---------------------------------------------------------------------------
// ReconcileToPhysicalLocked — on OWNED→PASSTHROUGH, bring emitted state
// into agreement with physical state. For keys physically down but not
// emitted: emit make. For keys emitted but not physically down: emit break.
// ---------------------------------------------------------------------------
void Engine::ReconcileToPhysicalLocked(
    const Directions &dirs, std::vector<VirtualAction> &actions) noexcept {
  const std::array<uint16_t, 4> keys{
      {dirs.forward, dirs.left, dirs.back, dirs.right}};

  for (const uint16_t sc : keys) {
    if (!IsValidKeyIndex(sc)) {
      continue;
    }

    const bool desired = keyStates_[sc].physicalDown;
    if (keyStates_[sc].emittedDown != desired) {
      actions.emplace_back(sc, desired);
    }
  }
}

// ---------------------------------------------------------------------------
// DesiredStateLocked — what a movement key should be.
// Handles both owned (trigger held) and non-owned SnapTap modes.
// ---------------------------------------------------------------------------
bool Engine::DesiredStateLocked(uint16_t sc) const noexcept {
  if (!IsValidKeyIndex(sc)) {
    return false;
  }

  const bool snaptap = snaptapEnabled_.load(std::memory_order_relaxed);

  if (ownershipActive_) {
    // Owned: spam phase controls on/off, SnapTap controls which key.
    if (!spamPhaseDown_) {
      return false; // spam up phase: all movement off
    }
    if (!snaptap) {
      return keyStates_[sc].physicalDown; // no SnapTap: mirror physical
    }
    return sc == activeVertical_ || sc == activeHorizontal_;
  }

  // Not owned: SnapTap still resolves axes if enabled.
  if (!snaptap) {
    return keyStates_[sc].physicalDown; // passthrough: mirror physical
  }
  return sc == activeVertical_ || sc == activeHorizontal_;
}

void Engine::UpdateActiveAxesLocked(const Directions &dirs) noexcept {
  if (!snaptapEnabled_.load(std::memory_order_relaxed)) {
    activeVertical_ = 0;
    activeHorizontal_ = 0;
    return;
  }

  activeVertical_ = ResolveAxisActiveLocked(dirs.forward, dirs.back);
  activeHorizontal_ = ResolveAxisActiveLocked(dirs.left, dirs.right);
}

// ---------------------------------------------------------------------------
// RunWatchdogLocked — periodic invariant audit (Phase 5: covers all states)
// ---------------------------------------------------------------------------
void Engine::RunWatchdogLocked(std::vector<VirtualAction> &actions) noexcept {
  const Directions dirs = LoadDirections();
  const std::array<uint16_t, 4> keys{
      {dirs.forward, dirs.left, dirs.back, dirs.right}};

  for (const uint16_t sc : keys) {
    if (!IsValidKeyIndex(sc)) {
      continue;
    }

    // DesiredStateLocked handles owned, SnapTap, and passthrough modes.
    const bool desired = DesiredStateLocked(sc);

    if (keyStates_[sc].emittedDown != desired) {
      actions.emplace_back(sc, desired);
    }
  }
}

// ---------------------------------------------------------------------------
// ForceReleaseAllVirtual — shutdown safety
// ---------------------------------------------------------------------------
void Engine::ForceReleaseAllVirtual() noexcept {
  const Directions dirs = LoadDirections();
  std::vector<VirtualAction> actions;

  {
    std::lock_guard<std::mutex> lock(stateMutex_);
    ownershipActive_ = false;
    triggerHeld_ = false;
    spamPhaseDown_ = false;
    activeVertical_ = 0;
    activeHorizontal_ = 0;

    for (const uint16_t sc : {dirs.forward, dirs.left, dirs.back, dirs.right}) {
      if (!IsValidKeyIndex(sc)) {
        continue;
      }
      if (!keyStates_[sc].emittedDown) {
        continue;
      }
      actions.emplace_back(sc, false);
    }

    EmitActionsLocked(actions);
  }
}

// ---------------------------------------------------------------------------
// EmitActionsLocked — inject synthetic keys, update emittedDown ONLY on
// confirmed success. Must hold stateMutex_.
// ---------------------------------------------------------------------------
void Engine::EmitActionsLocked(
    const std::vector<VirtualAction> &actions) noexcept {
  for (const auto &action : actions) {
    const uint16_t sc = action.first;
    const bool down = action.second;
    if (backend_.InjectKey(sc, down ? NEO_KEY_MAKE : NEO_KEY_BREAK)) {
      if (IsValidKeyIndex(sc)) {
        keyStates_[sc].emittedDown = down;
      }
    }
    // If InjectKey fails, emittedDown is NOT updated.
    // The next cycle will see the mismatch and retry.
  }
}

// ---------------------------------------------------------------------------
// PassThroughAndTrackLocked — forward non-swallowed events to the OS and
// update emittedDown for movement keys based on PassThrough success.
// Must hold stateMutex_.
// ---------------------------------------------------------------------------
void Engine::PassThroughAndTrackLocked(const std::vector<NEO_KEY_EVENT> &batch,
                                       const std::vector<uint8_t> &swallow,
                                       const Directions &dirs) noexcept {
  for (size_t i = 0; i < batch.size(); ++i) {
    if (swallow[i] != 0u) {
      continue;
    }

    const NEO_KEY_EVENT &evt = batch[i];
    const bool ok = backend_.PassThrough(evt);

    // Track emittedDown for passed-through movement keys.
    const uint16_t sc = evt.scanCode;
    if (ok && IsMovementKey(sc, dirs) && IsValidKeyIndex(sc)) {
      keyStates_[sc].emittedDown = IsKeyDown(evt);
    }
  }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
Engine::Directions Engine::LoadDirections() const noexcept {
  Directions d{};
  d.forward = directionScancodes_[0].load(std::memory_order_relaxed);
  d.left = directionScancodes_[1].load(std::memory_order_relaxed);
  d.back = directionScancodes_[2].load(std::memory_order_relaxed);
  d.right = directionScancodes_[3].load(std::memory_order_relaxed);
  return d;
}

bool Engine::IsMovementKey(uint16_t sc, const Directions &dirs) const noexcept {
  return sc == dirs.forward || sc == dirs.left || sc == dirs.back ||
         sc == dirs.right;
}

uint16_t Engine::ResolveAxisActiveLocked(uint16_t first,
                                         uint16_t second) const noexcept {
  if (!IsValidKeyIndex(first) && !IsValidKeyIndex(second)) {
    return 0;
  }

  const bool firstDown =
      IsValidKeyIndex(first) ? keyStates_[first].physicalDown : false;
  const bool secondDown =
      IsValidKeyIndex(second) ? keyStates_[second].physicalDown : false;

  if (!firstDown && !secondDown) {
    return 0;
  }
  if (firstDown && !secondDown) {
    return first;
  }
  if (!firstDown && secondDown) {
    return second;
  }

  const uint64_t firstSeq =
      IsValidKeyIndex(first) ? keyStates_[first].lastPressSeq : 0;
  const uint64_t secondSeq =
      IsValidKeyIndex(second) ? keyStates_[second].lastPressSeq : 0;
  return (secondSeq > firstSeq) ? second : first;
}

bool Engine::HasActiveDirectionLocked() const noexcept {
  return activeVertical_ != 0u || activeHorizontal_ != 0u;
}
