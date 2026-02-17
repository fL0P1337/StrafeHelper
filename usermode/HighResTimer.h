#pragma once
/*
 * HighResTimer.h
 * QPC-based high-resolution timing for x64 Windows 10+.
 * Zero heap allocations. Header-only.
 */

#include <cstdint>
#include <intrin.h>
#include <windows.h>


class HighResTimer {
  int64_t frequency_;
  int64_t origin_;

public:
  HighResTimer() noexcept {
    LARGE_INTEGER f, o;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&o);
    frequency_ = f.QuadPart;
    origin_ = o.QuadPart;
  }

  [[nodiscard]] int64_t NowTicks() const noexcept {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
  }

  [[nodiscard]] int64_t NowUs() const noexcept {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return ((now.QuadPart - origin_) * 1'000'000) / frequency_;
  }

  [[nodiscard]] int64_t NowNs() const noexcept {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return ((now.QuadPart - origin_) * 1'000'000'000) / frequency_;
  }

  [[nodiscard]] int64_t TicksToUs(int64_t ticks) const noexcept {
    return (ticks * 1'000'000) / frequency_;
  }

  [[nodiscard]] int64_t UsToTicks(int64_t us) const noexcept {
    return (us * frequency_) / 1'000'000;
  }

  [[nodiscard]] int64_t Frequency() const noexcept { return frequency_; }

  void SpinWaitUs(int64_t microseconds) const noexcept {
    LARGE_INTEGER target;
    QueryPerformanceCounter(&target);
    target.QuadPart += (frequency_ * microseconds) / 1'000'000;

    LARGE_INTEGER now;
    do {
      _mm_pause();
      QueryPerformanceCounter(&now);
    } while (now.QuadPart < target.QuadPart);
  }

  void PreciseWaitUs(int64_t microseconds) const noexcept {
    if (microseconds <= 0)
      return;

    LARGE_INTEGER deadline, now;
    QueryPerformanceCounter(&deadline);
    deadline.QuadPart += (frequency_ * microseconds) / 1'000'000;

    /* Coarse sleep for bulk of the duration */
    if (microseconds > 2000) {
      int64_t sleepUs = microseconds - 1500;
      DWORD sleepMs = static_cast<DWORD>(sleepUs / 1000);
      if (sleepMs > 0) {
        Sleep(sleepMs);
      }
    }

    /* Spin-wait for final precision */
    do {
      _mm_pause();
      QueryPerformanceCounter(&now);
    } while (now.QuadPart < deadline.QuadPart);
  }

  [[nodiscard]] int64_t ElapsedUsSince(int64_t startTicks) const noexcept {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return ((now.QuadPart - startTicks) * 1'000'000) / frequency_;
  }
};
