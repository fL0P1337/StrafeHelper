# 🔒 [Security] Fix Unsafe DLL Loading in Backends

## 🎯 What:
The vulnerability fixed relates to unsafe DLL loading in `InterceptionBackend.cpp` and `gui/GuiManager.cpp` where `LoadLibraryW` was used to load dynamic libraries. This has been replaced with the safer `LoadLibraryExW` along with flags restricting the search paths.

## ⚠️ Risk:
The potential impact if left unfixed is that an attacker could place a malicious `interception.dll` or `interception64.dll` in a working directory or other uncontrolled location in the DLL search path. When the application calls `LoadLibraryW(fullPath.c_str())`, if the target DLL has its own dependencies, those dependencies could be resolved from these untrusted directories, leading to arbitrary code execution within the context of the application (DLL Hijacking).

## 🛡️ Solution:
The fix addresses the vulnerability by using `LoadLibraryExW` with the flags `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`. This ensures that when loading the DLL, Windows will only search for its dependencies in the directory containing the DLL itself, the application's directory, System32, and user directories, explicitly excluding the potentially untrusted current working directory.
