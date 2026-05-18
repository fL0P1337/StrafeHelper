#include <iostream>
#include <cassert>
#include <atomic>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "../SuperglideLogic.h"
#include "../globals.h"
#include "../Config.h"

// Define the globals required for compilation and linking
namespace Globals {
  HANDLE g_hSuperglideThread = nullptr;
  HANDLE g_hSuperglideEvent = nullptr;
  SuperglideStats g_superglideStats{};
}

namespace Config {
  std::atomic<bool> EnableSuperglide{true};
  std::atomic<double> TargetFPS{60.0};
}

// Mocks for Windows API functions
extern "C" {
  void timeBeginPeriod(DWORD uPeriod) {}
  void timeEndPeriod(DWORD uPeriod) {}
}

std::atomic<int> mockSendInputCount{0};
std::vector<int> mockSendInputVKs;
std::mutex vkMutex;
std::atomic<long long> currentQpc{0};
std::atomic<bool> waitShouldReturn{false};
std::atomic<bool> threadReady{false};

DWORD SendInput(DWORD cInputs, INPUT *pInputs, int cbSize) {
    mockSendInputCount++;
    std::lock_guard<std::mutex> lock(vkMutex);
    if (cInputs > 0 && pInputs != nullptr) {
        mockSendInputVKs.push_back(pInputs[0].ki.wScan);
    }
    return cInputs;
}

BOOL QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency) {
    lpFrequency->QuadPart = 10000000; // 10MHz (10000 ticks per ms)
    return TRUE;
}

BOOL QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount) {
    lpPerformanceCount->QuadPart = currentQpc.load();
    // Advance QPC by a small amount to break out of busy wait loop
    currentQpc += 20000; // 2 ms per call - ensure we easily pass the target
    return TRUE;
}

std::mutex waitMutex;
std::condition_variable waitCv;

DWORD WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds) {
    if (hHandle == Globals::g_hSuperglideEvent) {
        std::unique_lock<std::mutex> lock(waitMutex);
        threadReady = true;
        waitCv.wait(lock, [] { return waitShouldReturn.load(); });
        waitShouldReturn = false;
        threadReady = false;
        return 0; // WAIT_OBJECT_0
    }
    if (dwMilliseconds != INFINITE) {
        // Sleep to mimic actual thread wait
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return 0; // WAIT_OBJECT_0
}

BOOL CloseHandle(HANDLE hObject) {
    return TRUE;
}

BOOL SetEvent(HANDLE hEvent) {
    if (hEvent == Globals::g_hSuperglideEvent) {
        std::unique_lock<std::mutex> lock(waitMutex);
        waitShouldReturn = true;
        waitCv.notify_all();
    }
    return TRUE;
}

void Sleep(DWORD dwMilliseconds) {
    currentQpc += dwMilliseconds * 10000; // advance QPC by sleep ms
}

DWORD (WINAPI *g_SuperglideThreadFunc)(LPVOID) = nullptr;

HANDLE CreateThread(LPVOID lpThreadAttributes, SIZE_T dwStackSize, DWORD (WINAPI *lpStartAddress)(LPVOID), LPVOID lpParameter, DWORD dwCreationFlags, LPDWORD lpThreadId) {
    g_SuperglideThreadFunc = lpStartAddress;
    return (HANDLE)0x1234;
}

// Mocks for Utils
DWORD VirtualKeyToScanCode(int vk) { return (DWORD)vk; }
DWORD VirtualKeyInputFlags(int vk, bool keyDown) { return keyDown ? 0 : KEYEVENTF_KEYUP; }
void LogError(const std::string& message, DWORD errorCode) {}

void test_SuperglideThread_StartStop() {
    Globals::g_hSuperglideThread = nullptr;

    bool started = StartSuperglideThread();
    assert(started);
    assert(Globals::g_hSuperglideThread != nullptr);
    assert(g_SuperglideThreadFunc != nullptr);

    StopSuperglideThread();
    assert(Globals::g_hSuperglideThread == nullptr);
}

void test_SuperglideThread_Logic() {
    std::cout << "Testing SuperglideLogic Thread..." << std::endl;
    // reset state
    mockSendInputCount = 0;
    mockSendInputVKs.clear();
    currentQpc = 0;
    waitShouldReturn = false;
    threadReady = false;
    Globals::g_superglideStats.count = 0;

    // Call StartSuperglideThread to set up g_stopSuperglideRequest=false and capture func ptr
    StartSuperglideThread();

    Globals::g_hSuperglideEvent = (HANDLE)0x5678;

    std::thread runner([]() {
        if (g_SuperglideThreadFunc) {
            g_SuperglideThreadFunc(nullptr);
        }
    });

    // Wait until the thread is actually blocking in WaitForSingleObject
    while (!threadReady.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Signal the event to run one loop iteration
    SetEvent(Globals::g_hSuperglideEvent);

    // Wait for the inputs to be sent! The loop has a wait at the very end:
    // g_superglideCv.wait_for(lock, std::chrono::milliseconds(500), ...)
    // So threadReady will NOT be true immediately! It will sleep for 500ms
    // or until g_superglideCv is notified!
    // But we don't have access to g_superglideCv.
    // Instead we can just wait for mockSendInputCount to reach 2!
    for(int i = 0; i < 50; i++) {
        if (mockSendInputCount.load() >= 2) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Stop the thread gracefully - this sets g_stopSuperglideRequest=true
    // and calls g_superglideCv.notify_all(), waking it up!
    StopSuperglideThread();

    runner.join();

    assert(mockSendInputCount == 2);
    assert(mockSendInputVKs.size() == 2);
    // VK_SPACE is 0x20
    assert(mockSendInputVKs[0] == 0x20);
    // VK_LCONTROL is 0xA2
    assert(mockSendInputVKs[1] == 0xA2);

    // Verify stats were updated
    assert(Globals::g_superglideStats.count.load() == 1);
    std::cout << "Logic test passed!" << std::endl;
}

int main() {
    std::cout << "Running Superglide tests...\n";
    test_SuperglideThread_StartStop();
    test_SuperglideThread_Logic();
    std::cout << "All tests passed!\n";
    return 0;
}
