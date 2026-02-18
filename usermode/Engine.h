#pragma once

#include "Config.h"
#include "HighResTimer.h"
#include "InputBackend.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

class Engine {
public:
  Engine(InputBackend &backend, const RuntimeConfig &config) noexcept;

  void Run(std::atomic<bool> &running) noexcept;
  void SetEnabled(bool enabled) noexcept;
  [[nodiscard]] bool IsEnabled() const noexcept;
  [[nodiscard]] bool ToggleEnabled() noexcept;
  void ApplyConfig(const RuntimeConfig &config) noexcept;

  [[nodiscard]] uint16_t TriggerScanCode() const noexcept;
  [[nodiscard]] uint32_t SpamDownUs() const noexcept;
  [[nodiscard]] uint32_t SpamUpUs() const noexcept;

private:
  struct KeyState {
    bool physicalDown = false;
    bool emittedDown = false; // what the OS currently sees (confirmed)
    uint64_t lastPressSeq = 0;
  };

  struct Directions {
    uint16_t forward = 0;
    uint16_t left = 0;
    uint16_t back = 0;
    uint16_t right = 0;
  };

  using VirtualAction = std::pair<uint16_t, bool>; // <scancode, down>

  // Core pipeline (called under stateMutex_)
  void ProcessInputBatchLocked(const std::vector<NEO_KEY_EVENT> &batch,
                               std::vector<uint8_t> &swallow,
                               std::vector<VirtualAction> &actions) noexcept;
  void ApplyMovementDiffLocked(const Directions &dirs,
                               std::vector<VirtualAction> &actions) noexcept;
  void ReconcileToPhysicalLocked(const Directions &dirs,
                                 std::vector<VirtualAction> &actions) noexcept;
  [[nodiscard]] bool DesiredStateLocked(uint16_t sc) const noexcept;
  void UpdateActiveAxesLocked(const Directions &dirs) noexcept;
  void RunWatchdogLocked(std::vector<VirtualAction> &actions) noexcept;

  // Emission (called under stateMutex_ — updates emittedDown on confirm)
  void EmitActionsLocked(const std::vector<VirtualAction> &actions) noexcept;
  void PassThroughAndTrackLocked(const std::vector<NEO_KEY_EVENT> &batch,
                                 const std::vector<uint8_t> &swallow,
                                 const Directions &dirs) noexcept;

  // Shutdown
  void ForceReleaseAllVirtual() noexcept;

  // Helpers
  [[nodiscard]] Directions LoadDirections() const noexcept;
  [[nodiscard]] bool IsMovementKey(uint16_t sc,
                                   const Directions &dirs) const noexcept;
  [[nodiscard]] uint16_t
  ResolveAxisActiveLocked(uint16_t first, uint16_t second) const noexcept;
  [[nodiscard]] bool HasActiveDirectionLocked() const noexcept;

  [[nodiscard]] static bool IsKeyDown(const NEO_KEY_EVENT &evt) noexcept {
    return (evt.flags & NEO_KEY_BREAK) == 0u;
  }

  [[nodiscard]] static bool
  IsSyntheticEvent(const NEO_KEY_EVENT &evt) noexcept {
    return evt.nativeInformation == NEO_SYNTHETIC_INFORMATION;
  }

private:
  InputBackend &backend_;
  HighResTimer timer_;

  // Config (atomics — written by tray thread, read by engine thread)
  std::atomic<bool> enabled_{true};
  std::atomic<bool> snaptapEnabled_{true};
  std::atomic<bool> locked_{false};
  std::atomic<uint16_t> triggerScanCode_{0x2Eu};
  std::atomic<uint32_t> spamDownUs_{500};
  std::atomic<uint32_t> spamUpUs_{500};
  std::array<std::atomic<uint16_t>, 4> directionScancodes_{
      {std::atomic<uint16_t>{0x11}, std::atomic<uint16_t>{0x1E},
       std::atomic<uint16_t>{0x1F}, std::atomic<uint16_t>{0x20}}};

  // State (guarded by stateMutex_)
  std::array<KeyState, 256> keyStates_{};
  uint64_t pressSeqCounter_ = 0;

  bool triggerHeld_ = false;
  bool ownershipActive_ = false;
  bool spamPhaseDown_ = false;
  uint16_t activeVertical_ = 0;
  uint16_t activeHorizontal_ = 0;

  int64_t nextSpamToggleQpc_ = 0;

  mutable std::mutex stateMutex_;
};
