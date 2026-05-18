🎯 **What:**
Removed redundant `// <-- Add` comments from `#include` statements in `Application.cpp`, `AppWindow.cpp`, `KeyboardHook.cpp`, and `globals.h`.

💡 **Why:**
These comments are leftovers from when the includes were originally added and serve no functional purpose anymore. Removing them improves maintainability, tidiness, and readability of the codebase.

✅ **Verification:**
Verified using `git diff` that only the target text `// <-- Add` (and in one case, some trailing words) was safely removed, preserving the includes entirely, which guarantees no behavioral changes to the program.

✨ **Result:**
The codebase is cleaner, and developers no longer have to read outdated, redundant comments next to basic library includes.
