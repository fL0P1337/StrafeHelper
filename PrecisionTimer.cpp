// PrecisionTimer.cpp
#include "PrecisionTimer.h"

PrecisionTimer::PrecisionTimer() noexcept {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    m_frequency = freq.QuadPart;
}

LONGLONG PrecisionTimer::GetCurrentTicks() noexcept {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

LONGLONG PrecisionTimer::MsToTicks(double ms) const noexcept {
    return static_cast<LONGLONG>((ms * static_cast<double>(m_frequency)) / 1000.0);
}
