// PrecisionTimer.h
#pragma once

#include <windows.h>
#include <chrono>
#include <thread>
#include <type_traits>

class PrecisionTimer {
public:
    PrecisionTimer() noexcept;

    // Returns the current QueryPerformanceCounter value
    static LONGLONG GetCurrentTicks() noexcept;

    // Converts milliseconds to ticks based on QPC frequency
    LONGLONG MsToTicks(double ms) const noexcept;

    // Sleeps/spins until the target tick is reached or stop token requests stop.
    // Returns true if completed fully, false if stop was requested.
    template <typename StopToken>
    bool PreciseSleepUntil(LONGLONG targetTick, const StopToken& stopToken) const noexcept {
        const LONGLONG spinThreshold = m_frequency / 2000LL; // 0.5 ms in ticks
        LONGLONG now = GetCurrentTicks();

        while (now < targetTick) {
            if constexpr (!std::is_same_v<StopToken, std::nullptr_t>) {
                if (stopToken.stop_requested()) {
                    return false;
                }
            }

            const LONGLONG remaining = targetTick - now;
            if (remaining > spinThreshold) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            now = GetCurrentTicks();
        }

        return true;
    }

    // Sleeps/spins for a duration in milliseconds
    template <typename StopToken>
    bool PreciseSleep(double ms, const StopToken& stopToken) const noexcept {
        if (ms <= 0.0) return true;
        const LONGLONG start = GetCurrentTicks();
        const LONGLONG target = start + MsToTicks(ms);
        return PreciseSleepUntil(target, stopToken);
    }

    // Returns QPC frequency in ticks per second
    LONGLONG GetFrequency() const noexcept { return m_frequency; }

private:
    LONGLONG m_frequency;
};
