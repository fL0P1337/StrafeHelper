# Interception Bundle

NeoStrafeApp expects Interception runtime artifacts in this folder when
`input_backend = interception`.

Required layout:

```text
third_party/
  interception/
    interception.dll
    install-interception.exe
```

Runtime behavior:

1. `InterceptionBackend` calls `LoadLibraryW("interception.dll")`.
2. Main process validates Interception driver services (`keyboard`, `mouse`).
3. Engine starts only when runtime checks pass.
