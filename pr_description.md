🧹 Remove redundant `// <-- Add` comments on includes

🎯 **What:** Removed redundant `// <-- Add` comments from `#include` directives across `Application.cpp`, `globals.h`, `KeyboardHook.cpp`, and `AppWindow.cpp`.
💡 **Why:** These comments were leftovers from when the includes were originally added and no longer serve any purpose, cluttering the codebase. Removing them improves readability and maintainability.
✅ **Verification:** Verified the code compiles successfully without the redundant comments and checked by searching the codebase to ensure all instances were removed.
✨ **Result:** Cleaned up the `#include` sections in several files, making the codebase cleaner without changing any functionality.
