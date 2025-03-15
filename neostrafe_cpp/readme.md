# StrafeHelper for Apex Legends

StrafeHelper is a lightweight utility designed to enhance specific movement actions in Apex Legends. This program improves in-game mobility by refining strafing and other key-based actions, ultimately helping to boost your gaming performance.

## Features

- **WASD Strafing:**  
  Automatically simulates rapid key press events for WASD keys, enabling smoother and more responsive strafing movements.

- **TurboLoot:**  
  Optimizes looting actions by quickly simulating 'E' key presses for faster item pickups.

- **SnapTap:**  
  Enhances directional toggling by dynamically switching between primary and secondary movement keys (W, A, S, D) to refine your in-game movement mechanics.

- **Customizable Settings:**  
  Easily enable or disable each feature using the system tray menu. Adjust behavior to match your preferences directly while in-game.

## How It Works

StrafeHelper monitors keypress events on your system and injects simulated key events to improve movement actions, all while being optimized for low CPU usage. It leverages a combination of keyboard hooks, system tray integration, and multi-threaded event-driven logic to provide seamless enhancements for your Apex Legends gameplay.

## Installation

1. **Build the Project:**

   - **Using Visual Studio:**  
     Configure the project as a **Windows Application** (not Console) by setting the subsystem to `Windows` in the project settings.

   - **Using Command Line (cl.exe):**
     ```
     cl.exe /SUBSYSTEM:WINDOWS StrafeHelper.cpp /link
     ```

   - **Using MinGW (g++):**
     ```
     g++ StrafeHelper.cpp -o StrafeHelper.exe -mwindows
     ```

2. **Run the Application:**  
   Simply run the compiled executable. StrafeHelper will add an icon to your system tray, providing quick access to its settings.

## Usage

- **Activating Features:**  
  Use the tray icon to toggle features on or off:
  - **WASD Strafing:** Toggle rapid, synchronized key simulation.
  - **TurboLoot:** Enable automated 'E' key presses for looting.
  - **SnapTap:** Activate dynamic key swapping to refine directional movement.

- **Customization:**  
  Modify key bindings, timings, and other settings directly within the source code if needed.

## Disclaimer

This tool is intended for personal use to enhance gameplay in Apex Legends. Use it responsibly, and be aware of game policies regarding third-party software. The developer is not responsible for any repercussions arising from its use.

## Contributing

Contributions are welcome! Feel free to fork this repository and submit pull requests with improvements or bug fixes.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

## Acknowledgments

- Thanks to the Apex Legends community for their feedback and ideas.
- Special thanks to the developers and maintainers of the Windows API and various online resources that made this development possible.