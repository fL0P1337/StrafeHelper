#include <iostream>
#include <cassert>
#include "Utils.h"
#include "Config.h"

void test_ApplyJitter_Disabled() {
    Config::EnableJitter.store(false);
    DWORD result = ApplyJitter(100);
    assert(result == 100);
    std::cout << "test_ApplyJitter_Disabled passed." << std::endl;
}

void test_ApplyJitter_EnabledButZero() {
    Config::EnableJitter.store(true);
    Config::JitterMs.store(0);
    DWORD result = ApplyJitter(100);
    assert(result == 100);
    std::cout << "test_ApplyJitter_EnabledButZero passed." << std::endl;
}

void test_ApplyJitter_Enabled() {
    Config::EnableJitter.store(true);
    Config::JitterMs.store(10);

    // Test multiple times since it's random
    bool wasDifferent = false;
    for (int i = 0; i < 50; ++i) {
        DWORD result = ApplyJitter(100);
        assert(result >= 90 && result <= 110);
        if (result != 100) {
            wasDifferent = true;
        }
    }
    assert(wasDifferent);
    std::cout << "test_ApplyJitter_Enabled passed." << std::endl;
}

void test_ApplyJitter_ClampsTo1() {
    Config::EnableJitter.store(true);
    Config::JitterMs.store(100); // High jitter to potentially go < 1

    for (int i = 0; i < 100; ++i) {
        DWORD result = ApplyJitter(0); // Base delay 0, expecting it to clamp to 1 when random is <= 0
        assert(result >= 1);
    }
    std::cout << "test_ApplyJitter_ClampsTo1 passed." << std::endl;
}

void test_FormatVirtualKeyName_Hardcoded() {
    assert(FormatVirtualKeyName(VK_LCONTROL) == "Left Ctrl");
    assert(FormatVirtualKeyName(VK_SPACE) == "Space");
    assert(FormatVirtualKeyName(VK_XBUTTON2) == "Mouse X2");
    std::cout << "test_FormatVirtualKeyName_Hardcoded passed." << std::endl;
}

void test_FormatVirtualKeyName_Alphanumeric() {
    assert(FormatVirtualKeyName('A') == "A");
    assert(FormatVirtualKeyName('Z') == "Z");
    assert(FormatVirtualKeyName('0') == "0");
    assert(FormatVirtualKeyName('9') == "9");
    std::cout << "test_FormatVirtualKeyName_Alphanumeric passed." << std::endl;
}

void test_VirtualKeyToScanCode() {
    assert(VirtualKeyToScanCode(VK_SPACE) == 0x39);
    assert(VirtualKeyToScanCode(VK_LCONTROL) == 0x1D);
    assert(VirtualKeyToScanCode(VK_RCONTROL) == 0x1D); // (0xE01D & 0xFF)
    std::cout << "test_VirtualKeyToScanCode passed." << std::endl;
}

void test_VirtualKeyInputFlags() {
    assert(VirtualKeyInputFlags(VK_SPACE, true) == KEYEVENTF_SCANCODE);
    assert(VirtualKeyInputFlags(VK_SPACE, false) == (KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP));
    assert(VirtualKeyInputFlags(VK_RCONTROL, true) == (KEYEVENTF_SCANCODE | KEYEVENTF_EXTENDEDKEY));
    assert(VirtualKeyInputFlags(VK_RCONTROL, false) == (KEYEVENTF_SCANCODE | KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP));
    std::cout << "test_VirtualKeyInputFlags passed." << std::endl;
}

void test_GetExecutableDirectory() {
    std::wstring dir = GetExecutableDirectory();
    assert(dir == L"C:\\Mock\\Path\\");
    std::cout << "test_GetExecutableDirectory passed." << std::endl;
}

void test_LogError() {
    LogError("Test error message", 0);
    LogError("Test error message with code", 404);
    std::cout << "test_LogError passed." << std::endl;
}

int run_utils_tests() {
    std::cout << "Running test_Utils..." << std::endl;

    test_ApplyJitter_Disabled();
    test_ApplyJitter_EnabledButZero();
    test_ApplyJitter_Enabled();
    test_ApplyJitter_ClampsTo1();
    test_FormatVirtualKeyName_Hardcoded();
    test_FormatVirtualKeyName_Alphanumeric();
    test_VirtualKeyToScanCode();
    test_VirtualKeyInputFlags();
    test_GetExecutableDirectory();
    test_LogError();

    std::cout << "All Utils tests passed." << std::endl;
    return 0;
}
