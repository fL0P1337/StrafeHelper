# NeoStrafe C++

## Input Backends

Supported backends:

- `interception`
- `kbdhook`
- `rawinput`

First launch prompts for backend selection and writes `input_backend` to
`config.cfg`.

Interception-specific startup validation:

1. `LoadLibraryW("interception.dll")`
2. Service checks for `keyboard` and `mouse` using SCM APIs
3. Backend initialization and keyboard enumeration

## Runtime UX

- System tray icon is created on startup.
- Left click toggles macros enabled/disabled.
- Right click menu:
  - Enable/Disable macros
  - Reload config
  - Switch backend (Interception / Keyboard Hook / RawInput)
  - Exit

Tooltip state:

- `NeoStrafe - Enabled`
- `NeoStrafe - Disabled`

## Configuration

- Config file path: `config.cfg` in the executable directory.
- If missing, NeoStrafeApp creates it automatically with defaults.
- Core keys:
  - `snaptap = true|false`
  - `input_backend = interception|kbdhook|rawinput`
- Supported readable key names include: `A-Z`, `0-9`, `F1-F12`, `CTRL`, `ALT`,
  `SHIFT`, `SPACE`, `TAB`, `ESC`, `ENTER`.
- Internal engine logic still operates on scancodes.
