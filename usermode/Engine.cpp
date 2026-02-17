#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "Engine.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <thread>

#include <windows.h>

namespace {
constexpr uint32_t kEventDrainMax = 64;
constexpr int64_t kTriggerSettleUs = 400;
constexpr auto kWatchdogPeriod = std::chrono::milliseconds(50);

[[nodiscard]] bool IsValidKeyIndex(uint16_t sc) noexcept { return sc < 256u; }

[[nodiscard]] int64_t QueryQpc() noexcept {
  LARGE_INTEGER qpc{};
  if (!QueryPerformanceCounter(&qpc)) {
    return 0;
  }
  return qpc.QuadPart;
}
} // namespace

Engine::Engine(InputBackend &backend, const RuntimeConfig &config) noexcept
    : backend_(backend) {
  LARGE_INTEGER freq{};
  if (QueryPerformanceFrequency(&freq)) {
    qpcFreq_ = freq.QuadPart;
  }
  ApplyConfig(config);
}

void Engine::Run(std::atomic<bool> &running) noexcept {
  std::jthread spamThread([&](std::stop_token) { SpamThreadMain(running); });

  NEO_KEY_EVENT events[kEventDrainMax]{};
  std::vector<NEO_KEY_EVENT> batch;
  batch.reserve(kEventDrainMax);
  std::vector<uint8_t> swallow;
  swallow.reserve(kEventDrainMax);
  std::vector<VirtualAction> actions;
  actions.reserve(kEventDrainMax);

  auto nextWatchdog = std::chrono::steady_clock::now() + kWatchdogPeriod;

  while (running.load(std::memory_order_relaxed)) {
    backend_.WaitForData(static_cast<uint32_t>(kWatchdogPeriod.count()));
    if (!running.load(std::memory_order_relaxed)) {
      break;
    }

    batch.clear();
    for (;;) {
      const uint32_t count = backend_.PollEvents(events, kEventDrainMax);
      if (count == 0) {
        break;
      }
      batch.insert(batch.end(), events, events + count);
    }

    if (!batch.empty()) {
      swallow.assign(batch.size(), 0u);
      actions.clear();
      bool shouldNotify = false;

      ProcessInputBatch(batch, swallow, actions, shouldNotify);

      for (size_t i = 0; i < batch.size(); ++i) {
        if (swallow[i] == 0u) {
          (void)backend_.PassThrough(batch[i]);
        }
      }

      EmitActions(actions);
      if (shouldNotify) {
        NotifySpamThread();
      }
    }

    const auto now = std::chrono::steady_clock::now();
    if (now >= nextWatchdog) {
      // Advance in fixed steps (no drift based on "now").
      while (nextWatchdog <= now) {
        nextWatchdog += kWatchdogPeriod;
      }

      std::vector<VirtualAction> wdActions;
      wdActions.reserve(4);
      const Directions dirs = LoadDirections();

      {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (!triggerHeld_) {
          for (const uint16_t sc :
               {dirs.forward, dirs.left, dirs.back, dirs.right}) {
            if (!IsValidKeyIndex(sc)) {
              continue;
            }
            KeyState &s = keyStates_[sc];
            if (s.virtualDown && !s.physicalDown) {
              s.virtualDown = false;
              wdActions.emplace_back(sc, false);
            }
          }
        }
      }

      EmitActions(wdActions);
    }
  }

  NotifySpamThread();
  spamThread.join();
  ForceReleaseAllVirtual();
}

void Engine::SetEnabled(bool enabled) noexcept {
  enabled_.store(enabled, std::memory_order_relaxed);

  const Directions dirs = LoadDirections();
  std::vector<VirtualAction> actions;
  bool stateDirty = false;
  bool shouldNotify = false;

  {
    std::lock_guard<std::mutex> lock(stateMutex_);
    const bool oldOwn = ownershipActive_;
    const uint16_t oldVertical = activeVertical_;
    const uint16_t oldHorizontal = activeHorizontal_;

    const bool shouldOwn = triggerHeld_ && enabled_.load(std::memory_order_relaxed) &&
                           !locked_.load(std::memory_order_relaxed);
    ownershipActive_ = shouldOwn;
    if (oldOwn != ownershipActive_) {
      stateDirty = true;
      shouldNotify = true;
    }
    if (!shouldOwn) {
      if (spamPhaseDown_) {
        spamPhaseDown_ = false;
        stateDirty = true;
      }
    }

    UpdateActiveAxesLocked(dirs);
    if (oldVertical != activeVertical_ || oldHorizontal != activeHorizontal_) {
      stateDirty = true;
      shouldNotify = true;
    }

    if (stateDirty) {
      ApplyStateDiffLocked(dirs, actions);
    }
    if (shouldNotify) {
      spamWake_ = true;
    }
  }

  EmitActions(actions);
  if (shouldNotify) {
    NotifySpamThread();
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

void Engine::ApplyConfig(const RuntimeConfig &config) noexcept {
  const uint32_t oldSpamDown = spamDownUs_.load(std::memory_order_relaxed);
  const uint32_t oldSpamUp = spamUpUs_.load(std::memory_order_relaxed);

  enabled_.store(config.enabled, std::memory_order_relaxed);
  snaptapEnabled_.store(config.snaptapEnabled, std::memory_order_relaxed);
  locked_.store(config.isLocked, std::memory_order_relaxed);
  triggerScanCode_.store(config.triggerScanCode, std::memory_order_relaxed);
  spamDownUs_.store((config.spamDownUs == 0) ? 1u : config.spamDownUs,
                    std::memory_order_relaxed);
  spamUpUs_.store((config.spamUpUs == 0) ? 1u : config.spamUpUs,
                  std::memory_order_relaxed);
  directionScancodes_[0].store(config.forwardScanCode, std::memory_order_relaxed);
  directionScancodes_[1].store(config.leftScanCode, std::memory_order_relaxed);
  directionScancodes_[2].store(config.backScanCode, std::memory_order_relaxed);
  directionScancodes_[3].store(config.rightScanCode, std::memory_order_relaxed);

  const Directions dirs = LoadDirections();
  std::vector<VirtualAction> actions;
  bool stateDirty = false;
  bool shouldNotify = false;

  {
    std::lock_guard<std::mutex> lock(stateMutex_);
    const bool oldOwn = ownershipActive_;
    const uint16_t oldVertical = activeVertical_;
    const uint16_t oldHorizontal = activeHorizontal_;

    const bool shouldOwn = triggerHeld_ && enabled_.load(std::memory_order_relaxed) &&
                           !locked_.load(std::memory_order_relaxed);
    ownershipActive_ = shouldOwn;
    if (oldOwn != ownershipActive_) {
      stateDirty = true;
      shouldNotify = true;
    }
    if (!shouldOwn) {
      if (spamPhaseDown_) {
        spamPhaseDown_ = false;
        stateDirty = true;
      }
    }

    UpdateActiveAxesLocked(dirs);
    if (oldVertical != activeVertical_ || oldHorizontal != activeHorizontal_) {
      stateDirty = true;
      shouldNotify = true;
    }

    if (stateDirty) {
      ApplyStateDiffLocked(dirs, actions);
    }

    if (ownershipActive_ ||
        oldSpamDown != spamDownUs_.load(std::memory_order_relaxed) ||
        oldSpamUp != spamUpUs_.load(std::memory_order_relaxed)) {
      shouldNotify = shouldNotify || ownershipActive_;
    }

    if (shouldNotify) {
      spamWake_ = true;
    }
  }

  EmitActions(actions);
  if (shouldNotify) {
    NotifySpamThread();
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

void Engine::ProcessInputBatch(const std::vector<NEO_KEY_EVENT> &batch,
                               std::vector<uint8_t> &swallow,
                               std::vector<VirtualAction> &actions,
                               bool &shouldNotify) noexcept {
  if (batch.empty()) {
    shouldNotify = false;
    return;
  }

  const Directions dirs = LoadDirections();
  const bool canSuppress = backend_.CanSuppressPhysical();
  const bool enabled = enabled_.load(std::memory_order_relaxed);
  const bool locked = locked_.load(std::memory_order_relaxed);
  const bool snaptap = snaptapEnabled_.load(std::memory_order_relaxed);
  const uint16_t triggerSc = triggerScanCode_.load(std::memory_order_relaxed);

  bool stateDirty = false;
  shouldNotify = false;

  if (swallow.size() != batch.size()) {
    swallow.assign(batch.size(), 0u);
  }

  {
    std::lock_guard<std::mutex> lock(stateMutex_);

    const bool triggerHeldStart = triggerHeld_;

    // Phase A: update physical state for entire batch (no reconciliation here).
    for (size_t i = 0; i < batch.size(); ++i) {
      const NEO_KEY_EVENT &evt = batch[i];
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
          stateDirty = true;
        }
        if (!oldPhysical && isDown) {
          keyStates_[sc].lastPressSeq = ++pressSeqCounter_;
        }
      }

      if (sc == triggerSc && triggerHeld_ != isDown) {
        triggerHeld_ = isDown;
        stateDirty = true;
        shouldNotify = true;

        const int64_t toggleQpc = (evt.timestampQpc != 0) ? evt.timestampQpc : QueryQpc();
        if (toggleQpc != 0) {
          int64_t freq = qpcFreq_;
          if (freq <= 0) {
            LARGE_INTEGER f{};
            if (QueryPerformanceFrequency(&f)) {
              freq = f.QuadPart;
              qpcFreq_ = freq;
            }
          }

          if (freq > 0) {
            const int64_t settleTicks = (kTriggerSettleUs * freq) / 1'000'000;
            const int64_t until = toggleQpc + settleTicks;
            triggerSettleUntilQpc_ = std::max(triggerSettleUntilQpc_, until);
            reconcileDeferred_ = true;
          }
        }
      }
    }

    // Phase B: resolve axes once after physical updates.
    const uint16_t oldVertical = activeVertical_;
    const uint16_t oldHorizontal = activeHorizontal_;
    if (snaptap) {
      UpdateActiveAxesLocked(dirs);
    }
    if (oldVertical != activeVertical_ || oldHorizontal != activeHorizontal_) {
      stateDirty = true;
      shouldNotify = true;
    }

    // Phase C: update ownership once.
    const bool shouldOwn = triggerHeld_ && enabled && !locked;
    if (ownershipActive_ != shouldOwn) {
      ownershipActive_ = shouldOwn;
      if (spamPhaseDown_ != shouldOwn) {
        spamPhaseDown_ = shouldOwn;
      }
      stateDirty = true;
      shouldNotify = true;
    }

    // Phase D: compute swallow per event (simulate trigger transitions), and update
    // virtualDown for movement keys that are passed through.
    bool triggerSim = triggerHeldStart;
    for (size_t i = 0; i < batch.size(); ++i) {
      const NEO_KEY_EVENT &evt = batch[i];
      if (IsSyntheticEvent(evt)) {
        swallow[i] = 1u;
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
      const bool sw = isMovement && ownSim && canSuppress;
      swallow[i] = sw ? 1u : 0u;

      if (isMovement && !sw && IsValidKeyIndex(sc)) {
        if (keyStates_[sc].virtualDown != isDown) {
          keyStates_[sc].virtualDown = isDown;
          stateDirty = true;
        }
      }
    }

    // Phase E: single reconciliation (or defer if inside trigger settle window).
    if (stateDirty || reconcileDeferred_) {
      const int64_t nowQpc = QueryQpc();
      if (nowQpc != 0 && nowQpc < triggerSettleUntilQpc_) {
        reconcileDeferred_ = true;
      } else {
        ApplyStateDiffLocked(dirs, actions);
        reconcileDeferred_ = false;
      }
    }

    if (shouldNotify) {
      spamWake_ = true;
    }
  }
}

void Engine::ApplyStateDiffLocked(const Directions &dirs,
                                  std::vector<VirtualAction> &actions) noexcept {
  const std::array<uint16_t, 4> keys{{dirs.forward, dirs.left, dirs.back, dirs.right}};

  for (const uint16_t sc : keys) {
    if (!IsValidKeyIndex(sc)) {
      continue;
    }

    const bool desiredDown = DesiredStateLocked(sc);
    KeyState &state = keyStates_[sc];
    if (state.virtualDown == desiredDown) {
      continue;
    }

    state.virtualDown = desiredDown;
    actions.emplace_back(sc, desiredDown);
  }
}

bool Engine::DesiredStateLocked(uint16_t sc) const noexcept {
  if (!IsValidKeyIndex(sc)) {
    return false;
  }

  const bool snaptap = snaptapEnabled_.load(std::memory_order_relaxed);
  const bool own = ownershipActive_;

  if (own) {
    if (!spamPhaseDown_) {
      return false;
    }
    if (!snaptap) {
      return keyStates_[sc].physicalDown;
    }
    return sc == activeVertical_ || sc == activeHorizontal_;
  }

  if (!snaptap) {
    return keyStates_[sc].physicalDown;
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
      if (!keyStates_[sc].virtualDown) {
        continue;
      }
      keyStates_[sc].virtualDown = false;
      actions.emplace_back(sc, false);
    }
  }

  EmitActions(actions);
}

void Engine::SpamThreadMain(std::atomic<bool> &running) noexcept {
  while (running.load(std::memory_order_relaxed)) {
    std::unique_lock<std::mutex> lock(stateMutex_);

    spamCv_.wait(lock, [&]() {
      const bool canOwn = ownershipActive_ && enabled_.load(std::memory_order_relaxed) &&
                          !locked_.load(std::memory_order_relaxed);
      return !running.load(std::memory_order_relaxed) || canOwn || spamWake_;
    });

    if (!running.load(std::memory_order_relaxed)) {
      break;
    }

    spamWake_ = false;

    while (running.load(std::memory_order_relaxed) && ownershipActive_ &&
           enabled_.load(std::memory_order_relaxed) &&
           !locked_.load(std::memory_order_relaxed)) {
      const bool hasActiveDirection =
          (activeVertical_ != 0u) || (activeHorizontal_ != 0u);
      if (!hasActiveDirection) {
        std::vector<VirtualAction> idleActions;
        if (spamPhaseDown_) {
          spamPhaseDown_ = false;
          ApplyStateDiffLocked(LoadDirections(), idleActions);
        }

        lock.unlock();
        EmitActions(idleActions);
        lock.lock();

        spamCv_.wait(lock, [&]() {
          return !running.load(std::memory_order_relaxed) ||
                 !ownershipActive_ || !enabled_.load(std::memory_order_relaxed) ||
                 locked_.load(std::memory_order_relaxed) || spamWake_ ||
                 activeVertical_ != 0u || activeHorizontal_ != 0u;
        });
        spamWake_ = false;
        continue;
      }

      const uint32_t periodUs = spamPhaseDown_
                                    ? std::max<uint32_t>(1u, SpamDownUs())
                                    : std::max<uint32_t>(1u, SpamUpUs());

      const bool interrupted = spamCv_.wait_for(
          lock, std::chrono::microseconds(periodUs), [&]() {
            return !running.load(std::memory_order_relaxed) || !ownershipActive_ ||
                   !enabled_.load(std::memory_order_relaxed) ||
                   locked_.load(std::memory_order_relaxed) || spamWake_;
          });

      if (!running.load(std::memory_order_relaxed) || !ownershipActive_ ||
          !enabled_.load(std::memory_order_relaxed) ||
          locked_.load(std::memory_order_relaxed)) {
        break;
      }

      if (interrupted) {
        spamWake_ = false;
        continue;
      }

      spamPhaseDown_ = !spamPhaseDown_;
      const Directions dirs = LoadDirections();
      std::vector<VirtualAction> actions;
      ApplyStateDiffLocked(dirs, actions);

      lock.unlock();
      EmitActions(actions);
      lock.lock();
    }
  }
}

void Engine::NotifySpamThread() noexcept {
  {
    std::lock_guard<std::mutex> lock(stateMutex_);
    spamWake_ = true;
  }
  spamCv_.notify_one();
}

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

void Engine::EmitActions(const std::vector<VirtualAction> &actions) noexcept {
  if (actions.empty()) {
    return;
  }

  std::lock_guard<std::mutex> injectLock(injectMutex_);
  for (const auto &action : actions) {
    const uint16_t sc = action.first;
    const bool down = action.second;
    (void)backend_.InjectKey(sc, down ? NEO_KEY_MAKE : NEO_KEY_BREAK);
  }
}
