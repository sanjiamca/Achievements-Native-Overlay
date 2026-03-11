# 🏆 Achievements Native In-Game Overlay

A powerful, native in-game overlay designed specifically for [PSerban93's Achievements](https://github.com/PSerban93/Achievements) application. 

## 🚀 Why this project?
Standard external overlays often fail in **Exclusive Fullscreen** or **Fullscreen Borderless** modes, especially in DirectX 9, 10, and 11 games (like Far Cry Primal). This project solves that by hooking directly into the game's render pipeline.

## ✨ Features
- **NATIVE RENDERING:** Drawn by the game engine itself. Works in ALL display modes.
- **AUTO-SYNC:** Background polling ensures your progress is always up to date.
- **TOAST NOTIFICATIONS:** Sleek pop-ups for achievement unlocks.
- **CUSTOM HOTKEYS:** Rebind your toggle key directly in the menu.
- **PROCESS RADAR:** Warns you if `Achievements.exe` is not running.

## 🛠 Supported Graphics APIs
- DirectX 9, 10, 11, 12
- OpenGL & Vulkan

## 🙏 Credits
- Based on the **UniversalHookX** framework.
- Backend data provided by **PSerban93**.