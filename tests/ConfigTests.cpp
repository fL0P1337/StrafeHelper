#include "Config.h"
#include <iostream>
#include <cassert>
#include <fstream>
#include <string>
#include <cstdio> // For std::remove

// Include Config.cpp directly to access anonymous namespace functions
#define TESTING_CONFIG_CPP
#include "../Config.cpp"

void TestParseKeyValue() {
    using namespace Config;
    // We are now inside the same translation unit as the anonymous namespace in Config.cpp

    // Basic keys
    assert(ParseKeyValue("a", 0) == 'A');
    assert(ParseKeyValue("A", 0) == 'A');
    assert(ParseKeyValue("1", 0) == '1');

    // Aliases
    assert(ParseKeyValue("ctrl", 0) == 0xA2); // VK_LCONTROL
    assert(ParseKeyValue("SPACE", 0) == 0x20); // VK_SPACE
    assert(ParseKeyValue("Mouse4", 0) == 0x05); // VK_XBUTTON1

    // Hex and integer inputs
    assert(ParseKeyValue("0x41", 0) == 0x41); // 'A'
    assert(ParseKeyValue("65", 0) == 65); // 'A'

    // Edge cases and fallbacks
    assert(ParseKeyValue("", 999) == 999);
    assert(ParseKeyValue("   ", 999) == 999);
    assert(ParseKeyValue("unknown_key", 999) == 999);

    std::cout << "TestParseKeyValue passed." << std::endl;
}

void TestLoadAndSaveConfig() {
    using namespace Config;

    // Ensure file doesn't exist
    std::remove(CONFIG_FILE_NAME);

    // Setup a clean state
    std::ofstream out(CONFIG_FILE_NAME);
    out << "SPAM_DELAY_MS=42\n";
    out << "enable_spam=true\n";
    out << "KEY_SPAM_TRIGGER=space\n";
    out.close();

    LoadConfig();

    assert(SpamDelayMs.load() == 42);
    assert(EnableSpam.load() == true);
    assert(KeySpamTrigger.load() == 0x20); // VK_SPACE

    // Modify and save
    SpamDelayMs.store(1337);
    SaveConfig();

    // Verify file contents
    std::ifstream in(CONFIG_FILE_NAME);
    std::string line;
    bool foundDelay = false;
    while (std::getline(in, line)) {
        if (line.find("SPAM_DELAY_MS = 1337") != std::string::npos) {
            foundDelay = true;
        }
    }
    in.close();
    assert(foundDelay);

    std::remove(CONFIG_FILE_NAME); // Cleanup
    std::cout << "TestLoadAndSaveConfig passed." << std::endl;
}

int main() {
    TestParseKeyValue();
    TestLoadAndSaveConfig();
    std::cout << "All tests passed successfully!" << std::endl;
    return 0;
}
