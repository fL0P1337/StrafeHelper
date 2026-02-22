# StrafeHelper

![StrafeHelper Showcase 1](showcase_1.jpg)

## Motivation

The main motivation to create this project is to stop pay-to-cheat apps for macroing a simple thing. By providing a transparent, open-source alternative, this project aims to level the playing field and offer a free solution for the movement community.

## Technical Details

- **Language**: C++ (C++17 or later recommended).
- **Concurrency**: Utilizes multi-threaded logic and atomic operations (`std::atomic`) for thread-safe configuration management and input handling.
- **Input Simulation**:
  - Uses the Win32 `SendInput` API for keyboard event injection.
  - Supports two **pluggable input backends** (see below) for capturing physical key presses.
- **Architecture**:
  - **InputBackend interface**: An abstract layer (`InputBackend.h`) decouples device I/O from all higher-level processing. Both backends implement `Initialize`, `PollEvents`, `PassThrough`, `InjectKey`, and `Shutdown`.
  - **SpamLogic & TurboLogic**: Handles the asynchronous timing and batching of simulated key presses.
  - **Config System**: Thread-safe loading and management of application parameters.
  - **GUI Integration**: A modern GUI layer powered by **Dear ImGui** over Win32 + DX11, completely isolated from the low-level keyboard hook to ensure zero-latency inputs while retaining a premium aesthetic.

## Previews

<div align="center">
  <img src="showcase_2.jpg" alt="StrafeHelper Showcase 2" width="45%" />
  <img src="showcase_3.jpg" alt="StrafeHelper Showcase 3" width="45%" />
</div>

## Features

- **Modern UI**: Easily toggle features and monitor statuses through the stunning ImGui interface.
- **WASD Strafing**: Synchronized, rapid key simulation for movement keys to enable perfect strafes.
- **Dynamic Triggering**: Activation via a customizable trigger key (e.g., toggle or hold) including modern conveniences like *SnapTap*.
- **In-Memory Configuration**: Settings can be modified in real-time through the new ImGui Config Panel or directly via `config.cfg`.
- **Low Footprint**: Optimized to ensure minimal CPU and memory usage during gameplay.
- **Dual Input Backends**: Runtime-switchable keyboard capture with live availability status in the UI.

## Input Backends

StrafeHelper supports two keyboard capture backends, selectable at runtime from the **Config** tab without restarting the application.

### WinHook *(default)*

Uses the Windows `WH_KEYBOARD_LL` low-level keyboard hook. Works on any system with no additional drivers required. This is the default and recommended mode for most users.

| | |
|---|---|
| Requires driver | ✗ No |
| Can suppress physical keys | ✗ No (uses `CallNextHookEx`) |
| Availability | Always |

### Interception *(optional)*

Uses the [Interception](https://github.com/oblitum/Interception) kernel-mode keyboard filter driver, loaded dynamically at runtime via `interception.dll`. Operates below the Windows input stack, giving reliable low-level access without hook latency.

| | |
|---|---|
| Requires driver | ✓ Yes — [Interception driver](https://github.com/oblitum/Interception) must be installed |
| Requires DLL | ✓ `interception.dll` (or `interception64.dll`) next to the exe |
| Can suppress physical keys | ✓ Yes |
| Availability | Only when driver is installed and running |

#### Installing the Interception driver

1. Download and install the [Interception driver](https://github.com/oblitum/Interception/releases).
2. Place `interception.dll` in the same folder as `StrafeHelper.exe`.
3. Launch StrafeHelper — the Config tab will show **`[ OK ] Interception driver ready`** in green when everything is detected correctly.

#### Live status indicator

The Input Backend section shows a color-coded status badge that updates every second:

| Badge | Color | Meaning |
|-------|-------|---------|
| `[ OK ]` | Green | Driver installed, DLL found, context opens successfully |
| `[ X ]` | Red | `interception.dll` not found next to the exe |
| `[ ! ]` | Amber | DLL present but the kernel driver is not running |

The **Interception** radio button is automatically grayed out when the driver is not available.

## Getting Started

### Prerequisites

- Visual Studio 2022 or a compatible C++ compiler.
- Windows SDK.

### Build Instructions

1. Open `StrafeHelper.sln` in Visual Studio.
2. Set the build configuration to **Release** and architecture to **x64**.
3. Build the solution. The output binary will be located in the `x64/Release/` folder.

## Usage

1. Run the compiled executable `StrafeHelper.exe`.
2. The modern GUI will launch. You can configure your keybinds visually, use the console, and monitor your physical and simulated outputs in the State Monitor.
3. Use the configured trigger key to activate the strafe logic in-game.
4. Optionally switch the **Input Backend** in the Config tab to use the Interception driver if you have it installed.

## Disclaimer

This tool is for educational and personal use. Always respect the terms of service of the games you play. The developers are not responsible for any misuse or consequences arising from the use of this software.

## License

This project is licensed under the MIT License.

