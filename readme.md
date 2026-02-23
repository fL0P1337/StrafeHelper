# StrafeHelper

![StrafeHelper Showcase 1](showcase_1.jpg)

## Motivation

Stop paying for macro apps that do one simple thing. StrafeHelper is a transparent, open-source movement utility for the PC FPS community — free, auditable, and built to stay that way.

---

## Features

| Feature | Description |
|---|---|
| **Macro Spam** | Rapid key cycling on WASD for strafing. Delay and hold-duration configurable. |
| **SnapTap / SOCD** | Last-input-wins axis filtering — press both A+D and only the most-recent registers. |
| **Turbo Loot** | Auto-repeats a loot key at configurable speed while held. |
| **Turbo Jump** | Auto-repeats a jump key at configurable speed while held. |
| **Superglide** | One-press automation of the Jump → (1 frame) → Crouch sequence with sub-millisecond QPC timing. |
| **Dual Input Backends** | WH_KEYBOARD_LL hook (default) or Interception kernel driver, hot-switchable at runtime. |
| **ImGui GUI** | Tabbed interface: Config, State Monitor, Console. All binds rebindable in-app. |

---

## Superglide

Superglide automates the precise Jump + Crouch timing required to execute a superglide in Apex Legends (and similar titles).

**How it works:**
1. Press your designated Superglide bind.
2. The bind is **silently suppressed** — the game never sees the physical keypress.
3. StrafeHelper injects:
   - **Jump** (Space)
   - Wait exactly **1 frame** at your configured FPS
   - **Crouch** (Left Ctrl)

**Timing precision:**  
Frame duration is computed as `freq / targetFPS` in QPC ticks (no integer truncation). A hybrid wait — coarse `Sleep(1)` + QPC spin for the final 0.5 ms — achieves sub-millisecond accuracy at any frame rate.

**Config options (in-app or `config.cfg`):**

| Key | Default | Description |
|---|---|---|
| `enable_superglide` | `false` | Master toggle |
| `superglide_bind` | `192` (tilde `~`) | VK code of the trigger key |
| `target_fps` | `60.0` | Game frame rate — determines 1-frame delay |

A 500 ms cooldown after each execution prevents accidental re-trigger from key-hold repeat.

---

## Input Backends

Both backends are selectable from the **Config** tab at runtime without restarting.

### WinHook *(default)*

`WH_KEYBOARD_LL` low-level keyboard hook. No driver required.

| | |
|---|---|
| Driver required | ✗ |
| Physical key suppression | ✗ (passthrough only) |

### Interception *(optional)*

Kernel-mode filter driver via [Interception](https://github.com/oblitum/Interception). Operates below the Windows input stack.

| | |
|---|---|
| Driver required | ✓ [Interception driver](https://github.com/oblitum/Interception/releases) |
| DLL required | ✓ `interception.dll` next to the exe |
| Physical key suppression | ✓ |

**Setup:** Install the driver, place `interception.dll` next to the exe, launch StrafeHelper. The Config tab shows a live status badge:

| Badge | Color | Meaning |
|---|---|---|
| `[ OK ]` | Green | Driver ready |
| `[ X ]` | Red | `interception.dll` not found |
| `[ ! ]` | Amber | DLL present, driver not running |

---

## Technical Details

- **Language:** C++20 (MSVC v143)
- **GUI:** Dear ImGui · Win32 + DirectX 11 backend · Inter font
- **Threading:** One Win32 thread + auto-reset event per feature; atomic config shared across threads
- **Timing:** `QueryPerformanceFrequency` / `QueryPerformanceCounter` for frame-accurate sleep; `timeBeginPeriod(1)` for coarse sleep resolution
- **Injection:** `SendInput` with `KEYEVENTF_SCANCODE`; synthetic events tagged with `NEO_SYNTHETIC_INFORMATION` to avoid re-entrant processing

### Architecture

```
InputBackend (abstract)
├── KbdHookBackend   — WH_KEYBOARD_LL hook thread → event queue → PollEvents
└── InterceptionBackend — Interception driver polling thread → event queue → PollEvents

Application
├── DispatchKeyEvent  — routes Interception events to feature handlers
├── KeyboardProc      — routes WinHook events to feature handlers
└── Feature threads (Win32 HANDLE + Event)
    ├── SpamLogic
    ├── TurboLogic (Loot + Jump)
    └── SuperglideLogic

Gui::GuiManager  — ImGui render loop (Config / Monitor / Console tabs)
Config namespace — atomic<T> vars + LoadConfig / SaveConfig (key=value file)
```

---

## Build

1. Open **Visual Studio 2022** (v143 toolset).
2. Open `StrafeHelper.sln`.
3. Set configuration to **Release | x64**.
4. Build — output at `x64/Release/StrafeHelper.exe`.

Or from PowerShell:

```powershell
$msbuild = Get-ChildItem "C:\Program Files\Microsoft Visual Studio\*\Community\MSBuild" -Recurse -Filter "MSBuild.exe" | Select-Object -First 1 -ExpandProperty FullName
& $msbuild StrafeHelper.sln /p:Configuration=Release /p:Platform=x64 /m
```

---

## Previews

<div align="center">
  <img src="showcase_2.jpg" alt="Config Tab" width="45%" />
  <img src="showcase_3.jpg" alt="State Monitor" width="45%" />
</div>

---

## Disclaimer

For educational and personal use only. Respect the terms of service of the games you play. The authors are not responsible for any consequences arising from use of this software.

## License

MIT — see [LICENSE.txt](LICENSE.txt).
