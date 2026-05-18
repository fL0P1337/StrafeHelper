#include <iostream>
#include <string>
#include <cassert>

#include "mock/windows.h"
// Include Config.cpp directly to test the unexposed ParseKeyValue function
#include "../Config.cpp"

void TestParseKeyValue() {
    std::cout << "Running TestParseKeyValue...\n";

    // Access it from Config::(anonymous namespace)
    // Actually, ParseKeyValue is in Config::(anonymous namespace)
    // By including the file, we can just call ParseKeyValue directly because we are compiling the same translation unit.

    // Test empty string fallback
    assert(Config::ParseKeyValue("", 999) == 999);
    assert(Config::ParseKeyValue("   ", 888) == 888);

    // Test single character parsing (should uppercase and return ascii code)
    assert(Config::ParseKeyValue("a", 0) == 'A');
    assert(Config::ParseKeyValue("A", 0) == 'A');
    assert(Config::ParseKeyValue("1", 0) == '1');

    // Test alias parsing
    assert(Config::ParseKeyValue("space", 0) == VK_SPACE);
    assert(Config::ParseKeyValue("SPACE", 0) == VK_SPACE); // case insensitivity check
    assert(Config::ParseKeyValue("  space  ", 0) == VK_SPACE); // trim check

    assert(Config::ParseKeyValue("ctrl", 0) == VK_LCONTROL);
    assert(Config::ParseKeyValue("shift", 0) == VK_SHIFT);
    assert(Config::ParseKeyValue("mouse4", 0) == VK_XBUTTON1);

    // Test integer parsing fallback
    assert(Config::ParseKeyValue("123", 0) == 123);

    // Test unknown string fallback
    assert(Config::ParseKeyValue("unknown_key_string", 777) == 777);

    std::cout << "TestParseKeyValue passed.\n";
}

int main() {
    TestParseKeyValue();
    return 0;
}
