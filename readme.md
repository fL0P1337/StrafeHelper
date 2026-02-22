# StrafeHelper
## Motivation

The main motivation to create this project is stop p2c apps for macroing a such simple thing. By providing a transparent, open-source alternative, this project aims to level the playing field and offer a free solution for movement community.

## Technical Details

- **Language**: C++ (C++17 or later recommended).
- **Concurrency**: Utilizes multi-threaded logic and atomic operations (`std::atomic`) for thread-safe configuration management and input handling.
- **Input Simulation**:
  - Uses the Win32 `SendInput` API for keyboard event injection.
  - Implements low-level keyboard hooks (`WH_KEYBOARD_LL`) to monitor physical key states without latency.
- **Architecture**:
  - **SpamLogic**: Handles the asynchronous timing and batching of simulated key presses.
  - **Config System**: Thread-safe loading and management of application parameters.
  - **Tray Integration**: Minimalistic UI using the Windows system tray for real-time control.

## Branches and Implementation Variants

This repository contains different approaches to input handling:

- **master**: The main branch containing the current stable implementation and core logic.
- **structbased-kbdhook-impl**: A variant focusing on a structured approach using Windows Low-Level Keyboard Hooks (`SetWindowsHookEx`). This method is ideal for broad compatibility without requiring custom drivers.
- **structbased-interception-impl**: An advanced variant that utilizes the **Interception Driver**. This allows for deeper interaction with the input stack, potentially bypassing certain software-level detection while providing even lower latency.
- **structbased-1.0-kbdhook-impl**: A legacy/milestone branch for the initial structured keyboard hook implementation.

## Features

- **WASD Strafing**: Synchronized, rapid key simulation for movement keys to enable perfect strafes.
- **Dynamic Triggering**: Activation via a customizable trigger key (e.g., toggle or hold).
- **In-Memory Configuration**: Settings can be modified in real-time through the tray icon or configuration file.
- **Low Footprint**: Optimized to ensure minimal CPU and memory usage during gameplay.

## Getting Started

### Prerequisites

- Visual Studio 2022 or a compatible C++ compiler.
- Windows SDK.

### Build Instructions

1. Open the solution file in Visual Studio.
2. Set the build configuration to **Release** and architecture to **x64**.
3. Ensure the Project Subsystem is set to **Windows** (`/SUBSYSTEM:WINDOWS`) to avoid a console window popup (unless in Debug mode).
4. Build the solution.

## Usage

1. Run the compiled executable.
2. An icon will appear in the system tray.
3. Right-click the icon to toggle features or exit the application.
4. Use the configured trigger key to activate the strafe logic in-game.

## Disclaimer

This tool is for educational and personal use. Always respect the terms of service of the games you play. The developers are not responsible for any misuse or consequences arising from the use of this software.

## License

This project is licensed under the MIT License.
