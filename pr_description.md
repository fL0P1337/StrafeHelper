# 🧪 [testing improvement] Add test suite for Utils.cpp

🎯 **What:**
Added a comprehensive test file for `Utils.cpp` under the Linux-compatible unit test suite. `Utils.cpp` contains important core shared utilities such as precision random jitter calculation, virtual key conversion and formatting, robust path detection, and error logging wrapper functions used heavily throughout the multi-threaded system. Testing them directly ensures the backbone of input generation and system state evaluation remains reliable.

📊 **Coverage:**
The newly added `test_Utils.cpp` covers:
- **Jitter logic (`ApplyJitter`):** Verified to remain untouched when disabled, handle edge cases like a jitter scale of zero, apply correct randomized delays inside bounds, and accurately clamp the lower bound to 1 (preventing infinite loops/zero sleep delays).
- **Virtual key parsing (`FormatVirtualKeyName`):** Correctly mapped hardcoded strings, evaluated alphanumeric single-char fallbacks, and effectively utilized mocked API methods for Windows-dependent lookup fallbacks (e.g., `GetKeyNameTextA`).
- **Input flag generation (`VirtualKeyToScanCode`, `VirtualKeyInputFlags`):** Correct flags generated based on extended keys, key up events, and simulated scan codes using the mocked `MapVirtualKeyW`.
- **System util functions (`GetExecutableDirectory`, `LogError`):** Evaluated buffer loading constraints using mocked Windows paths, confirming safe std::string manipulations and error output piping via `std::cerr` without crashing.

✨ **Result:**
The `Utils.cpp` tests integrate smoothly into the existing `Makefile`-based test runner. This brings isolated validation to previously untested pure functions and wraps the Windows APIs in mocked interfaces, greatly improving test coverage and guaranteeing safety for future refactors of these highly shared utility components.
