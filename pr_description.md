🎯 **What:** Removed commented out `const UINT` versions of tray icon macro declarations, and a related outdated explanatory comment.

💡 **Why:** The codebase already uses `#define` macros for these constants for better switch-statement compatibility. Leaving commented-out alternative variable declarations is dead code, and the instructional comment on how to use `#define` adds visual clutter once the migration to macros is complete. Removing these artifacts improves code readability.

✅ **Verification:** Verified that only comments and dead code blocks were affected. Used `git diff` to ensure no active code or definitions were removed. Since this was purely a removal of commented-out text, functional behavior remains completely unaltered.

✨ **Result:** Cleaned up `globals.h` by removing unnecessary boilerplate comments, enhancing clarity for future maintenance.
