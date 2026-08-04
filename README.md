# Vertical Shorts Plugin for OBS Studio

**Version 1.1.0** — Windows

Vertical production dock for [OBS Studio](https://obsproject.com): Shorts, TikTok, Reels, and Twitch vertical.

## Download

- **Setup.exe:** [Vertical-Shorts-Plugin-Setup.exe](https://github.com/PFS2689/verticalshorts/releases/latest/download/Vertical-Shorts-Plugin-Setup.exe)
- **Zip:** [Vertical-Shorts-Plugin.zip](https://github.com/PFS2689/verticalshorts/releases/latest/download/Vertical-Shorts-Plugin.zip)

## UI overview

Dock title: **Vertical Shorts**

- Workspace switch: **Vertical Layout** (default) / **Horizontal Layout**
- OBS-style panels: Scenes, Sources, Audio Mixer, Transitions
- Canvas controls (bottom-right): 🟢 Go Live · ⏺️ Record · 📸 Clip · ⚙️ Settings
- Vertical transforms are stored on private mirror scenes so your main horizontal layout is not overwritten

## Install

1. Close OBS Studio  
2. Run Setup.exe **or** copy `obs-shorts-vertical` into `%APPDATA%\obs-studio\plugins`  
3. Start OBS → **View → Docks → Vertical Shorts**

## Requirements

- Windows 10/11 64-bit  
- OBS Studio **31+** (built against 31.1.1)

## Build

```bash
# Windows (VS 2022) — see .github/workflows/build-windows.yaml
cmake --preset windows-x64
cmake --build --preset windows-x64-release
```

Optional settings tests: configure with `-DVSP_BUILD_TESTS=ON`.

## License

GPL-2.0-or-later (same as OBS Studio).
