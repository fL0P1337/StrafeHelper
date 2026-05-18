🧪 Add unit tests for Config::ParseKeyValue

🎯 **What:**
Added a standalone test suite and mocked dependencies to verify the `ParseKeyValue` logic in `Config.cpp`. Missing tests for mapping key names, numeric virtual key fallback, and boundary cases were causing blind spots.

📊 **Coverage:**
* Validates normal lowercase/uppercase letters maps properly to uppercase ASCII bounds
* Verifies known key aliases are successfully mapped to `VK_*` codes (e.g. "space", "left_ctrl", "mouse4")
* Checks robust handling of spaces, underscores, mixed case in identifiers
* Confirms numeric virtual keys act as expected
* Tests fallbacks on empty or unknown items

✨ **Result:**
The core input parsing configuration logic is now protected from unobserved regressions by using mocked Windows API primitives in a platform-independent unit test.
