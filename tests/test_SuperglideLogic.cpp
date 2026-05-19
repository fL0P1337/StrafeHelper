#include <iostream>
#include <cassert>
#include "SuperglideLogic.h"
#include "Globals.h"

bool g_mockCreateThreadShouldFail = false;

void test_StartSuperglideThread_Success() {
    g_mockCreateThreadShouldFail = false;
    Globals::g_hSuperglideThread = NULL; // reset state

    bool result = StartSuperglideThread();

    assert(result == true);
    assert(Globals::g_hSuperglideThread != NULL);
    std::cout << "test_StartSuperglideThread_Success passed." << std::endl;
}

void test_StartSuperglideThread_Failure() {
    g_mockCreateThreadShouldFail = true;
    Globals::g_hSuperglideThread = NULL; // reset state

    bool result = StartSuperglideThread();

    assert(result == false);
    assert(Globals::g_hSuperglideThread == NULL);
    std::cout << "test_StartSuperglideThread_Failure passed." << std::endl;
}

int run_superglide_tests() {
    std::cout << "Running test_SuperglideLogic..." << std::endl;

    test_StartSuperglideThread_Success();
    test_StartSuperglideThread_Failure();

    std::cout << "All tests passed." << std::endl;
    return 0;
}
