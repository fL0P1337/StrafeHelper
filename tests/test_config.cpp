#include <iostream>
#include <string>
#include <cassert>
#include <vector>

// Include the source file directly to access the anonymous namespace
#include "../Config.cpp"

#define ASSERT_EQ(expected, actual, msg) \
    do { \
        if ((expected) != (actual)) { \
            std::cerr << "[FAIL] " << msg << " - expected " << (expected) << ", got " << (actual) << std::endl; \
            exit(1); \
        } \
    } while (0)

void test_ParseKeyValue() {
    std::cout << "Testing ParseKeyValue..." << std::endl;

    // Single characters
    ASSERT_EQ('A', Config::ParseKeyValue("a", 0), "Lowercase letter");
    ASSERT_EQ('A', Config::ParseKeyValue("A", 0), "Uppercase letter");
    ASSERT_EQ('Z', Config::ParseKeyValue("z", 0), "Lowercase letter boundary");
    ASSERT_EQ('1', Config::ParseKeyValue("1", 0), "Number");

    // Aliases
    ASSERT_EQ(VK_SPACE, Config::ParseKeyValue("space", 0), "Space alias");
    ASSERT_EQ(VK_SPACE, Config::ParseKeyValue(" SPACE ", 0), "Space alias with padding");
    ASSERT_EQ(VK_SPACE, Config::ParseKeyValue("SpaceBar", 0), "Spacebar alias mixed case");
    ASSERT_EQ(VK_LCONTROL, Config::ParseKeyValue("left_ctrl", 0), "Alias with underscore");
    ASSERT_EQ(VK_LCONTROL, Config::ParseKeyValue("lctrl", 0), "Alias abbreviation");
    ASSERT_EQ(VK_RETURN, Config::ParseKeyValue("enter", 0), "Enter alias");
    ASSERT_EQ(VK_XBUTTON1, Config::ParseKeyValue("mouse4", 0), "Mouse4 alias");

    // Numeric values
    ASSERT_EQ(123, Config::ParseKeyValue("123", 0), "Positive number");
    ASSERT_EQ(-42, Config::ParseKeyValue("-42", 0), "Negative number");

    // Edge cases and fallbacks
    ASSERT_EQ(99, Config::ParseKeyValue("", 99), "Empty string fallback");
    ASSERT_EQ(99, Config::ParseKeyValue("   ", 99), "Whitespace string fallback");
    ASSERT_EQ(99, Config::ParseKeyValue("unknown_key", 99), "Unknown alias fallback");

    std::cout << "[PASS] All ParseKeyValue tests passed!" << std::endl;
}

int main() {
    test_ParseKeyValue();
    return 0;
}
