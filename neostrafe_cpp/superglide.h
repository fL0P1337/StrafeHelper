#pragma once
#include <windows.h>
#include <mutex>
#include <atomic>
#include <chrono>
#include <thread>
#include "Config.h"
#include "inputs.h"

class SuperGlide {
private:
    static SuperGlide* instance;
    static std::mutex instanceMutex;
    std::atomic<bool> isRunning;
    std::thread workerThread;
    inputs::InputManager* inputManager;

    // SuperGlide state tracking
    std::atomic<bool> jumpPressed;
    std::atomic<bool> crouchPressed;
    std::atomic<bool> superGlideInProgress;
    std::chrono::steady_clock::time_point lastJumpTime;
    std::chrono::steady_clock::time_point lastCrouchTime;

    SuperGlide() :
        isRunning(false),
        jumpPressed(false),
        crouchPressed(false),
        superGlideInProgress(false),
        inputManager(nullptr) {
        lastJumpTime = std::chrono::steady_clock::now();
        lastCrouchTime = std::chrono::steady_clock::now();
    }

    void workerLoop() {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
        SetThreadAffinityMask(GetCurrentThread(), 1);

        while (isRunning.load()) {
            if (Config::getInstance()->isSuperGlideEnabled.load()) {
                processSuperGlide();
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }

    void processSuperGlide() {
        if (GetAsyncKeyState(Config::getInstance()->superGlideTriggerKey.load()) & 0x8000) {
            if (!jumpPressed.load()) {
                jumpPressed.store(true);
                lastJumpTime = std::chrono::steady_clock::now();

                // Execute jump
                inputManager->sendGameInput(VK_SPACE, true);
                std::this_thread::sleep_for(std::chrono::microseconds(
                    static_cast<int>(Config::getInstance()->superGlideJumpDelay.load() * 1000000)));
                inputManager->sendGameInput(VK_SPACE, false);

                // Schedule crouch with optimal timing
                auto crouchDelay = std::chrono::microseconds(
                    static_cast<int>(Config::getInstance()->superGlideCrouchDelay.load() * 1000000));
                std::this_thread::sleep_for(crouchDelay);

                // Execute crouch
                inputManager->sendGameInput(VK_CONTROL, true);
                std::this_thread::sleep_for(std::chrono::microseconds(100));
                inputManager->sendGameInput(VK_CONTROL, false);

                if (Config::getInstance()->showConsole.load()) {
                    std::cout << "[" << "2025-03-18 11:24:25" << "] SuperGlide executed with "
                        << Config::getInstance()->superGlideCrouchDelay.load() * 1000
                        << "ms crouch delay\n";
                }

                superGlideInProgress.store(true);
            }
        }
        else {
            if (jumpPressed.load()) {
                jumpPressed.store(false);
                superGlideInProgress.store(false);
            }
        }
    }

public:
    static SuperGlide* getInstance() {
        std::lock_guard<std::mutex> lock(instanceMutex);
        if (instance == nullptr) {
            instance = new SuperGlide();
        }
        return instance;
    }

    void initialize(inputs::InputManager* manager) {
        inputManager = manager;
        isRunning.store(true);
        workerThread = std::thread(&SuperGlide::workerLoop, this);
        SetThreadPriority(workerThread.native_handle(), THREAD_PRIORITY_TIME_CRITICAL);

        if (Config::getInstance()->showConsole.load()) {
            std::cout << "[" << "2025-03-18 11:24:25" << "] SuperGlide initialized\n"
                << "User: " << "fL0P1337" << "\n"
                << "Target FPS: " << Config::getInstance()->targetFPS.load() << "\n"
                << "Jump Delay: " << Config::getInstance()->superGlideJumpDelay.load() << "s\n"
                << "Crouch Delay: " << Config::getInstance()->superGlideCrouchDelay.load() << "s\n"
                << "Trigger Key: " << Config::getInstance()->superGlideTriggerKey.load() << "\n";
        }
    }

    void cleanup() {
        isRunning.store(false);
        if (workerThread.joinable()) {
            workerThread.join();
        }

        if (Config::getInstance()->showConsole.load()) {
            std::cout << "[" << "2025-03-18 11:24:25" << "] SuperGlide cleaned up\n";
        }
    }

    bool isEnabled() const {
        return Config::getInstance()->isSuperGlideEnabled.load();
    }

    void setEnabled(bool enabled) {
        Config::getInstance()->isSuperGlideEnabled.store(enabled);
        if (Config::getInstance()->showConsole.load()) {
            std::cout << "[" << "2025-03-18 11:24:25" << "] SuperGlide "
                << (enabled ? "enabled" : "disabled") << "\n";
        }
    }

    bool isSuperGliding() const {
        return superGlideInProgress.load();
    }

    ~SuperGlide() {
        cleanup();
    }
};

// Static member definitions
SuperGlide* SuperGlide::instance = nullptr;
std::mutex SuperGlide::instanceMutex;