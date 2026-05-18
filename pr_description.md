🧪 Add tests for VirtualKeyToScanCode

🎯 What:
Added missing unit tests for `VirtualKeyToScanCode` located in `Utils.cpp`. The codebase was missing automated validation for ensuring virtual keys map to the correct scan codes according to the mocked Windows API behavior.

📊 Coverage:
The new tests cover:
- Standard alphanumeric keys (e.g. `'A'` maps to `0x1E`)
- Control and system keys (e.g. `VK_SPACE` maps to `0x39`, `VK_ESCAPE` maps to `0x01`, `VK_RETURN` maps to `0x1C`)
- Extended keys handling (verifying that the extended bit is appropriately dropped by the bitmask logic, e.g. `VK_RCONTROL` returning `0x1D`)
- Unmapped/invalid keys returning `0x00`.

✨ Result:
Improved test coverage and reliability for `Utils.cpp`. Since `VirtualKeyToScanCode` acts as a crucial bridge between input representations, verifying its behavior guarantees that downstream functionality (like event injection) receives the correct key codes. This provides a safety net allowing confident refactoring.
