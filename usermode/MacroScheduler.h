#pragma once
/*
 * MacroScheduler.h
 * Deterministic macro scheduler with QPC-based timing.
 * Zero heap allocations. Fixed-capacity instruction slots.
 */

#include "HighResTimer.h"
#include "InputBackend.h"

#include <array>
#include <atomic>
#include <cstdint>

constexpr uint16_t INPUT_FLAG_BREAK = 0x0001u;

namespace ScanCodes {
constexpr uint16_t W = 0x11;
constexpr uint16_t A = 0x1E;
constexpr uint16_t S = 0x1F;
constexpr uint16_t D = 0x20;
constexpr uint16_t E = 0x12;
constexpr uint16_t C = 0x2E;
constexpr uint16_t LCTRL = 0x1D;
constexpr uint16_t SPACE = 0x39;
} // namespace ScanCodes

enum class MacroOp : uint8_t {
  Nop,
  KeyDown,
  KeyUp,
  DelayUs,
  LoopBegin,
  LoopEnd,
  End
};

struct MacroInstruction {
  MacroOp op;
  uint16_t scanCode;
  uint16_t flags;
  uint32_t param;
};

class KeyStateTable {
  struct State {
    std::atomic<bool> physicalDown{false};
    std::atomic<bool> spamActive{false};
  };
  std::array<State, 256> states_{};

public:
  [[nodiscard]] bool IsDown(uint8_t sc) const noexcept {
    return states_[sc].physicalDown.load(std::memory_order_relaxed);
  }
  void SetDown(uint8_t sc, bool v) noexcept {
    states_[sc].physicalDown.store(v, std::memory_order_relaxed);
  }
  [[nodiscard]] bool IsSpamming(uint8_t sc) const noexcept {
    return states_[sc].spamActive.load(std::memory_order_relaxed);
  }
  void SetSpamming(uint8_t sc, bool v) noexcept {
    states_[sc].spamActive.store(v, std::memory_order_relaxed);
  }
  void Reset() noexcept {
    for (auto &s : states_) {
      s.physicalDown.store(false, std::memory_order_relaxed);
      s.spamActive.store(false, std::memory_order_relaxed);
    }
  }
};

class MacroSlot {
public:
  static constexpr uint32_t MAX_INSTRUCTIONS = 64;

  std::array<MacroInstruction, MAX_INSTRUCTIONS> code{};
  uint32_t length = 0;
  uint32_t pc = 0;
  int64_t deadline = 0;
  uint32_t loopCounter = 0;
  uint32_t loopStart = 0;
  uint32_t loopMax = 0;
  bool active = false;
  bool looping = false;

  void Reset() noexcept {
    pc = 0;
    deadline = 0;
    loopCounter = 0;
    looping = false;
    active = false;
  }

  uint32_t Emit(MacroOp op, uint16_t sc = 0, uint16_t fl = 0,
                uint32_t p = 0) noexcept {
    if (length >= MAX_INSTRUCTIONS) {
      return length;
    }
    code[length] = {op, sc, fl, p};
    return length++;
  }
};

class MacroScheduler {
public:
  static constexpr uint32_t MAX_SLOTS = 8;

private:
  std::array<MacroSlot, MAX_SLOTS> slots_{};
  HighResTimer timer_;
  InputBackend *backend_ = nullptr;

public:
  explicit MacroScheduler(InputBackend *backend) noexcept : backend_(backend) {}

  MacroSlot &Slot(uint32_t idx) noexcept { return slots_[idx]; }

  void ActivateSlot(uint32_t idx) noexcept {
    if (idx < MAX_SLOTS && slots_[idx].length > 0) {
      slots_[idx].pc = 0;
      slots_[idx].deadline = 0;
      slots_[idx].loopCounter = 0;
      slots_[idx].looping = false;
      slots_[idx].active = true;
    }
  }

  void DeactivateSlot(uint32_t idx) noexcept {
    if (idx < MAX_SLOTS) {
      slots_[idx].active = false;
    }
  }

  void DeactivateAll() noexcept {
    for (auto &s : slots_) {
      s.active = false;
    }
  }

  void Tick() noexcept {
    const int64_t nowUs = timer_.NowUs();

    for (auto &slot : slots_) {
      if (!slot.active || slot.pc >= slot.length) {
        continue;
      }

      while (slot.active && slot.pc < slot.length) {
        if (slot.deadline > 0 && nowUs < slot.deadline) {
          break;
        }

        const auto &instr = slot.code[slot.pc];

        switch (instr.op) {
        case MacroOp::KeyDown:
          if (backend_) {
            (void)backend_->InjectKey(instr.scanCode, instr.flags);
          }
          ++slot.pc;
          break;

        case MacroOp::KeyUp:
          if (backend_) {
            (void)backend_->InjectKey(instr.scanCode,
                                      instr.flags | INPUT_FLAG_BREAK);
          }
          ++slot.pc;
          break;

        case MacroOp::DelayUs:
          slot.deadline = nowUs + static_cast<int64_t>(instr.param);
          ++slot.pc;
          break;

        case MacroOp::LoopBegin:
          slot.loopStart = slot.pc + 1;
          slot.loopMax = instr.param;
          slot.loopCounter = 0;
          slot.looping = true;
          ++slot.pc;
          break;

        case MacroOp::LoopEnd:
          if (slot.looping) {
            ++slot.loopCounter;
            if (slot.loopMax == 0 || slot.loopCounter < slot.loopMax) {
              slot.pc = slot.loopStart;
            } else {
              slot.looping = false;
              slot.loopCounter = 0;
              ++slot.pc;
            }
          } else {
            ++slot.pc;
          }
          break;

        case MacroOp::End:
          slot.active = false;
          break;

        case MacroOp::Nop:
        default:
          ++slot.pc;
          break;
        }
      }
    }
  }

  [[nodiscard]] const HighResTimer &Timer() const noexcept { return timer_; }
};

namespace MacroBuilder {
inline void BuildSpam(MacroSlot &slot, uint16_t scanCode, uint32_t downUs,
                      uint32_t upUs) noexcept {
  slot.length = 0;
  slot.Emit(MacroOp::LoopBegin, 0, 0, 0);
  slot.Emit(MacroOp::KeyDown, scanCode);
  slot.Emit(MacroOp::DelayUs, 0, 0, downUs);
  slot.Emit(MacroOp::KeyUp, scanCode);
  slot.Emit(MacroOp::DelayUs, 0, 0, upUs);
  slot.Emit(MacroOp::LoopEnd);
  slot.Emit(MacroOp::End);
}

inline void BuildTap(MacroSlot &slot, uint16_t scanCode,
                     uint32_t holdUs) noexcept {
  slot.length = 0;
  slot.Emit(MacroOp::KeyDown, scanCode);
  slot.Emit(MacroOp::DelayUs, 0, 0, holdUs);
  slot.Emit(MacroOp::KeyUp, scanCode);
  slot.Emit(MacroOp::End);
}
} // namespace MacroBuilder
