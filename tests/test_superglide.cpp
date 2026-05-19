#include <iostream>
#include <cassert>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include "../SuperglideLogic.h"
#include "../globals.h"
#include "../Config.h"

// Reference external mock state from stub_globals.cpp
extern std::vector<std::pair<int, bool>> g_mockInjectedKeys;
extern std::mutex g_mockInjectedKeysMutex;



void test_SuperglideThread_StartStop() {
    std::cout << "Testing Start/Stop lifecycle..." << std::endl;
    bool started = StartSuperglideThread();
    assert(started);
    
    // Stop thread and verify clean shutdown
    StopSuperglideThread();
    std::cout << "Start/Stop lifecycle test passed." << std::endl;
}

void test_SuperglideThread_Logic() {
    std::cout << "Testing SuperglideLogic Thread Execution..." << std::endl;

    // Reset mocked state
    {
        std::lock_guard<std::mutex> lock(g_mockInjectedKeysMutex);
        g_mockInjectedKeys.clear();
    }
    Globals::g_superglideStats.count.store(0);
    Globals::g_superglideStats.writeIdx.store(0);
    Config::EnableSuperglide.store(true);
    Config::TargetFPS.store(60.0);

    // Start superglide thread
    bool started = StartSuperglideThread();
    assert(started);

    // Give thread a moment to initialize and enter wait state
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Trigger the superglide macro
    TriggerSuperglide();

    // Wait for the thread to execute (Jump key, Sleep frame, Crouch key)
    // 60 FPS is ~16.6ms per frame. Let's wait up to 100ms.
    for (int i = 0; i < 10; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        std::lock_guard<std::mutex> lock(g_mockInjectedKeysMutex);
        if (g_mockInjectedKeys.size() >= 4) {
            break;
        }
    }

    // Stop the thread gracefully
    StopSuperglideThread();

    // Assert key sequence: Space (0x20) and Left Control (0xA2)
    std::lock_guard<std::mutex> lock(g_mockInjectedKeysMutex);
    std::cout << "Injected keys size: " << g_mockInjectedKeys.size() << std::endl;
    for (const auto& key : g_mockInjectedKeys) {
        std::cout << "  Key: " << key.first << " (Down: " << (key.second ? "True" : "False") << ")" << std::endl;
    }

    assert(g_mockInjectedKeys.size() == 4);
    
    // Space (Jump) down/up
    assert(g_mockInjectedKeys[0].first == 0x20); // VK_SPACE
    assert(g_mockInjectedKeys[0].second == true);
    assert(g_mockInjectedKeys[1].first == 0x20); // VK_SPACE
    assert(g_mockInjectedKeys[1].second == false);
    
    // Left Control (Crouch) down/up
    assert(g_mockInjectedKeys[2].first == 0xA2); // VK_LCONTROL
    assert(g_mockInjectedKeys[2].second == true);
    assert(g_mockInjectedKeys[3].first == 0xA2); // VK_LCONTROL
    assert(g_mockInjectedKeys[3].second == false);

    // Verify stats were correctly recorded
    assert(Globals::g_superglideStats.count.load() == 1);
    std::cout << "SuperglideLogic thread execution test passed!" << std::endl;
}

int run_superglide_tests() {
    std::cout << "Running Superglide tests...\n";
    test_SuperglideThread_StartStop();
    test_SuperglideThread_Logic();
    std::cout << "All tests passed successfully!\n";
    return 0;
}
