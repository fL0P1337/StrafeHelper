#include "../Utils.h"
#include <iostream>
#include <cassert>

void test_VirtualKeyToScanCode() {
    // Basic test case: Space mapping to 0x39
    assert(VirtualKeyToScanCode(VK_SPACE) == 0x39);

    // Alphanumeric keys
    assert(VirtualKeyToScanCode('A') == 0x1E);

    // System/control keys
    assert(VirtualKeyToScanCode(VK_ESCAPE) == 0x01);
    assert(VirtualKeyToScanCode(VK_RETURN) == 0x1C);

    // Extended keys usually map with the extended prefix, but VirtualKeyToScanCode
    // applies a mask `& 0xFFu` on the returned scan code, so it will drop extended bytes.
    // E.g., if MAPVK_VK_TO_VSC_EX gives 0xE01D, the function returns 0x1D
    assert(VirtualKeyToScanCode(VK_RCONTROL) == 0x1D);

    // Unmapped/invalid keys should fallback correctly if applicable, or return 0
    assert(VirtualKeyToScanCode(0x99) == 0x00);

    std::cout << "test_VirtualKeyToScanCode passed!" << std::endl;
}

int run_utils_tests() {
    test_VirtualKeyToScanCode();
    return 0;
}
