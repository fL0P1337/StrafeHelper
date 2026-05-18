🧪 [testing improvement] Add unit tests for StartSuperglideThread

🎯 **What:** The testing gap addressed
This PR introduces unit tests to verify the behavior of `StartSuperglideThread` in `SuperglideLogic.cpp`. This function is responsible for initializing and launching the background thread handling Superglide execution in the application. Before this PR, this functionality was completely untested, running the risk of unhandled failure scenarios causing instability.

📊 **Coverage:** What scenarios are now tested
- **Happy Path:** Tests that `StartSuperglideThread` successfully creates the thread and sets the thread handle `Globals::g_hSuperglideThread` as expected when Windows API `CreateThread` succeeds.
- **Failure Condition:** Tests the edge case where `CreateThread` fails (e.g. out of resources) and returns NULL. Verifies that the function appropriately returns `false`, logs an error using `LogError`, and keeps the `Globals::g_hSuperglideThread` handle as NULL.

✨ **Result:** The improvement in test coverage
By adding these tests, we provide a safety net for `SuperglideLogic.cpp`, explicitly guaranteeing that background thread initialization is both robust against API failures and functions appropriately in valid scenarios. This adds much-needed automated test coverage and confidence for core behavior. The test framework was also expanded with mock Windows APIs and mock Config structures for more isolated component testing moving forward.
