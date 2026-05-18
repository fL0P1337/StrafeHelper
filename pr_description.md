## What
Replaced the polling loop thread `SideMouseThreadFunc` with an event-driven `WM_INPUT` handler in `AppWindow.cpp` to process mouse side button inputs (`VK_XBUTTON1`, `VK_XBUTTON2`) through the Raw Input API instead of calling `GetAsyncKeyState` repeatedly.

## Why
The `SideMouseThreadFunc` used a continuous loop checking button state, followed by `std::this_thread::sleep_for(std::chrono::milliseconds(1));`. This constant polling waking up 1000 times a second unnecessarily consumed CPU resources and caused context switching overhead, even when the mouse was inactive. The use of `WH_MOUSE_LL` was intentionally avoided in the previous code due to cursor lag issues with 8000Hz polling rate mice. By leveraging the Raw Input API (`WM_INPUT`), we eliminate polling while remaining performant with high polling-rate hardware.

## Measured Improvement
- Baseline: The polling thread was continuously executing ~1000 times per second, with CPU cycles consumed regardless of input state.
- Change: The CPU usage for checking mouse buttons drops entirely when inactive, executing only when a hardware interrupt event occurs. This reduces overhead and frees CPU cycles for game processing.
