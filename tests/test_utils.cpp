#include "catch.hpp"
#include "../Utils.h"
#include "Config.h"

TEST_CASE("ApplyJitter behavior", "[Utils]") {
    SECTION("When Jitter is disabled, it returns baseDelay") {
        Config::EnableJitter.store(false);
        REQUIRE(ApplyJitter(100) == 100);
        REQUIRE(ApplyJitter(0) == 0);
    }

    SECTION("When Jitter is enabled but JitterMs is 0, it returns baseDelay") {
        Config::EnableJitter.store(true);
        Config::JitterMs.store(0);
        REQUIRE(ApplyJitter(100) == 100);
    }

    SECTION("When Jitter is enabled and JitterMs > 0, it returns within expected range") {
        Config::EnableJitter.store(true);
        Config::JitterMs.store(10);
        DWORD baseDelay = 100;

        bool all_equal = true;
        for (int i = 0; i < 100; ++i) {
            DWORD result = ApplyJitter(baseDelay);
            REQUIRE(result >= baseDelay - 10);
            REQUIRE(result <= baseDelay + 10);
            if (result != baseDelay) {
                all_equal = false;
            }
        }
        // Very unlikely to get exactly baseDelay 100 times in a row with JitterMs=10
        REQUIRE(all_equal == false);
    }

    SECTION("ApplyJitter ensures returned value is at least 1") {
        Config::EnableJitter.store(true);
        Config::JitterMs.store(10);
        DWORD baseDelay = 5; // Result could be 5 - 10 = -5

        for (int i = 0; i < 100; ++i) {
            DWORD result = ApplyJitter(baseDelay);
            REQUIRE(result >= 1);
        }
    }
}

TEST_CASE("VirtualKeyToScanCode behavior", "[Utils]") {
    SECTION("Basic mapping") {
        REQUIRE(VirtualKeyToScanCode(VK_SPACE) == 0x39);
    }

    SECTION("Extended keys mask mapping") {
        REQUIRE(VirtualKeyToScanCode(VK_RCONTROL) == 0x1D);
    }
}
