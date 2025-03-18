#pragma once
#include <mutex>
#include <atomic>
#include <chrono>
#include <windows.h>
#include <mmsystem.h>  // Add this for timeBeginPeriod and timeEndPeriod
#include <iostream>
#include "Config.h"
#include "InputTypes.h"

#pragma comment(lib, "winmm.lib")  // Link against Windows Multimedia library

// Last updated: 2025-03-18 06:31:46 UTC
// Author: fL0P1337

namespace inputs {
    class InputManager;  // Forward declaration
}

class SuperGlide {
private:
    static SuperGlide* instance;
    static std::mutex instanceMutex;

    // State management
    std::atomic<bool> isActive{ false };
    inputs::IInputManager* inputManager{ nullptr };

    // Precise frame timing constants for 144 FPS (in milliseconds)
    static constexpr double FRAME_TIME_144 = 1000.0 / 144.0;    // ~6.944ms per frame
    static constexpr double JUMP_DURATION = FRAME_TIME_144;      // Exactly one frame
    static constexpr double JUMP_TO_CROUCH_DELAY = 0.0;         // Immediate crouch after jump
    static constexpr double CROUCH_DURATION = 0.001;            // Minimal press duration

    // Private constructor for singleton
    SuperGlide() = default;

    friend class inputs::InputManager;  // Allow InputManager to access private members

public:
    SuperGlide(const SuperGlide&) = delete;
    SuperGlide& operator=(const SuperGlide&) = delete;

    static SuperGlide* getInstance() {
        std::lock_guard<std::mutex> lock(instanceMutex);
        if (instance == nullptr) {
            instance = new SuperGlide();
        }
        return instance;
    }

    void setInputManager(inputs::IInputManager* manager) {
        inputManager = manager;
    }

private:
    void executeSuperGlide() {
        if (!inputManager) return;

        try {
            // Disable timer resolution for maximum precision
            TIMECAPS tc;
            if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) == TIMERR_NOERROR) {
                timeBeginPeriod(tc.wPeriodMin);
            }

            // Calculate actual frame time
            double targetFPS = Config::getInstance()->targetFPS.load();
            double frameTime = 1000.0 / targetFPS;

            LARGE_INTEGER frequency, start;
            QueryPerformanceFrequency(&frequency);

            // Start timing
            QueryPerformanceCounter(&start);

            // Execute jump for exactly one frame
            inputManager->sendGameInput(VK_SPACE, true);
            preciseSleep(JUMP_DURATION);
            inputManager->sendGameInput(VK_SPACE, false);

            // Immediate crouch (no delay for perfect timing)
            inputManager->sendGameInput(VK_LCONTROL, true);
            preciseSleep(CROUCH_DURATION);
            inputManager->sendGameInput(VK_LCONTROL, false);

            // Reset timer resolution
            if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) == TIMERR_NOERROR) {
                timeEndPeriod(tc.wPeriodMin);
            }

            if (Config::getInstance()->showConsole.load()) {
                std::cout << "[2025-03-18 06:31:46] SuperGlide executed\n"
                    << "Target FPS: " << targetFPS << "\n"
                    << "Frame time: " << frameTime << "ms\n"
                    << "Jump duration: " << JUMP_DURATION << "ms\n"
                    << "Crouch delay: " << JUMP_TO_CROUCH_DELAY << "ms\n";
            }
        }
        catch (const std::exception& e) {
            TIMECAPS tc;
            if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) == TIMERR_NOERROR) {
                timeEndPeriod(tc.wPeriodMin);  // Make sure to reset timer resolution
            }
            if (Config::getInstance()->showConsole.load()) {
                std::cout << "[2025-03-18 06:31:46] Error: " << e.what() << "\n";
            }
        }
    }

    void preciseSleep(double milliseconds) {
        LARGE_INTEGER frequency;
        LARGE_INTEGER start;
        LARGE_INTEGER now;

        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&start);

        double millisecondsPassed;
        do {
            QueryPerformanceCounter(&now);
            millisecondsPassed = (now.QuadPart - start.QuadPart) * 1000.0 / frequency.QuadPart;

            // Yield remainder of time slice if we're not close to target
            if (milliseconds - millisecondsPassed > 0.1) {
                YieldProcessor();
            }
        } while (millisecondsPassed < milliseconds);
    }

public:
    void toggle() {
        bool newState = !isActive.load();

        if (Config::getInstance()->showConsole.load()) {
            std::cout << "[2025-03-18 06:31:46] SuperGlide "
                << (newState ? "Enabled" : "Disabled") << "\n";
        }

        isActive.store(newState);
    }

    bool isEnabled() const {
        return isActive.load() && Config::getInstance()->isSuperGlideEnabled.load();
    }

    ~SuperGlide() {
        if (Config::getInstance()->showConsole.load()) {
            std::cout << "[2025-03-18 06:31:46] SuperGlide destroyed\n";
        }
    }
};

// Static member definitions
SuperGlide* SuperGlide::instance = nullptr;
std::mutex SuperGlide::instanceMutex;