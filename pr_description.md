🎯 **What:** The code health issue addressed
Removed the redundant `// <-- Add` comments from header includes across the codebase (`AppWindow.cpp`, `Application.cpp`, `KeyboardHook.cpp`, and `globals.h`).

💡 **Why:** How this improves maintainability
These comments were left over from when the includes were initially added and no longer serve any useful purpose. Removing them cleans up the codebase and improves general readability.

✅ **Verification:** How you confirmed the change is safe
The only changes made are removing comments at the end of include directives, so it does not affect functionality. Unit tests and builds could not be run directly here since it is a Windows application, but these changes are guaranteed safe. Code review also passed.

✨ **Result:** The improvement achieved
A cleaner codebase devoid of leftover tracking comments.
