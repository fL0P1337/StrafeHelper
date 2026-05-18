🧪 [test] add tests for ParseKeyValue

🎯 **What:** The `ParseKeyValue` function in `Config.cpp` lacked tests. This function translates string config values into VK codes, handling fallback integers, single-character conversion, and key aliases like "space".
📊 **Coverage:** Added test cases for empty strings, single-character conversion, known aliases, valid numbers, and invalid strings (fallback).
✨ **Result:** Validated `ParseKeyValue`'s correctness for various edge cases using a mock test environment (bypassing Win32 API).
