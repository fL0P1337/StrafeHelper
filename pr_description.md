🧪 [Testing] Add unit tests for ApplyJitter function

🎯 **What:** The `ApplyJitter` function in `Utils.cpp` lacked tests. It takes a base delay and adds a random amount of jitter to it, based on atomic configuration values (`Config::EnableJitter` and `Config::JitterMs`).

📊 **Coverage:** The new tests in `test_utils.cpp` verify:
1. `ApplyJitter` returns `baseDelay` when `Config::EnableJitter` is false.
2. `ApplyJitter` returns `baseDelay` when `Config::JitterMs` is 0.
3. `ApplyJitter` returns a value within the expected range `[baseDelay - JitterMs, baseDelay + JitterMs]` when jitter is enabled and > 0.
4. `ApplyJitter` handles edge cases where the adjusted delay would be less than 1, ensuring the returned value is always at least 1.

✨ **Result:** Enhanced test coverage for utility functions, ensuring core timing logic functions as expected across different configurations.
