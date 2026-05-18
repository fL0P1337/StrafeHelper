🔒 Fix DLL hijacking vulnerability in DLL loading

🎯 **What:** Replaced `LoadLibraryW` with `LoadLibraryExW` using secure flags (`LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`) in `gui/GuiManager.cpp` and `InterceptionBackend.cpp`.
⚠️ **Risk:** Loading DLLs with `LoadLibraryW` can allow DLL hijacking if an attacker places a malicious DLL with the same name in the current working directory, potentially leading to arbitrary code execution.
🛡️ **Solution:** By using `LoadLibraryExW` with `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR` and `LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`, we ensure that the dependencies are resolved securely from the specified DLL directory or standard system directories, completely avoiding the attacker-controlled CWD.
