#pragma once

#include "Config.h"
#include "InputBackend.h"

#include <array>
#include <atomic>
#include <condition_variable>
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
    bool virtualDown = false;
    uint64_t lastPressSeq = 0;
  };

  struct Directions {
    uint16_t forward = 0;
    uint16_t left = 0;
    uint16_t back = 0;
    uint16_t right = 0;
  };

  using VirtualAction = std::pair<uint16_t, bool>; // <scancode, down>

  void ProcessInputBatch(const std::vector<NEO_KEY_EVENT> &batch,
                         std::vector<uint8_t> &swallow,
                         std::vector<VirtualAction> &actions,
                         bool &shouldNotify) noexcept;
  void ApplyStateDiffLocked(const Directions &dirs,
                            std::vector<VirtualAction> &actions) noexcept;
  [[nodiscard]] bool DesiredStateLocked(uint16_t sc) const noexcept;

  void UpdateActiveAxesLocked(const Directions &dirs) noexcept;
  void ForceReleaseAllVirtual() noexcept;

  void SpamThreadMain(std::atomic<bool> &running) noexcept;
  void NotifySpamThread() noexcept;

  [[nodiscard]] Directions LoadDirections() const noexcept;
  [[nodiscard]] bool IsMovementKey(uint16_t sc,
                                   const Directions &dirs) const noexcept;
  [[nodiscard]] uint16_t ResolveAxisActiveLocked(uint16_t first,
                                                 uint16_t second) const noexcept;
  void EmitActions(const std::vector<VirtualAction> &actions) noexcept;

  [[nodiscard]] static bool IsKeyDown(const NEO_KEY_EVENT &evt) noexcept {
    return (evt.flags & NEO_KEY_BREAK) == 0u;
  }

  [[nodiscard]] static bool IsSyntheticEvent(const NEO_KEY_EVENT &evt) noexcept {
    return evt.nativeInformation == NEO_SYNTHETIC_INFORMATION;
  }

private:
  InputBackend &backend_;

  std::atomic<bool> enabled_{true};
  std::atomic<bool> snaptapEnabled_{true};
  std::atomic<bool> locked_{false};
  std::atomic<uint16_t> triggerScanCode_{0x2Eu};
  std::atomic<uint32_t> spamDownUs_{500};
  std::atomic<uint32_t> spamUpUs_{500};
  std::array<std::atomic<uint16_t>, 4> directionScancodes_{{
      std::atomic<uint16_t>{0x11}, std::atomic<uint16_t>{0x1E},
      std::atomic<uint16_t>{0x1F}, std::atomic<uint16_t>{0x20}}};

  std::array<KeyState, 256> keyStates_{};
  uint64_t pressSeqCounter_ = 0;

  bool triggerHeld_ = false;
  bool ownershipActive_ = false;
  bool spamPhaseDown_ = false;
  uint16_t activeVertical_ = 0;
  uint16_t activeHorizontal_ = 0;

  int64_t qpcFreq_ = 0;
  int64_t triggerSettleUntilQpc_ = 0;
  bool reconcileDeferred_ = false;

  mutable std::mutex stateMutex_;
  std::condition_variable spamCv_;
  bool spamWake_ = false;

  std::mutex injectMutex_;
};
