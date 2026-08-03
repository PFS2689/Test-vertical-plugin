# Vertical Shorts Plugin for OBS Studio

**Version 1.0.0** — Windows one-click installer

Vertical canvas plugin for [OBS Studio](https://obsproject.com) made for **YouTube Shorts**, **TikTok**, and **Instagram Reels**.

## One-click install (Windows)

**[Download & run VerticalShortsPlugin-Setup.exe](https://github.com/PFS2689/vertical-shorts-plugin/releases/latest/download/VerticalShortsPlugin-Setup.exe)**

1. Close OBS Studio  
2. Run the installer (no admin required)  
3. Start OBS → **View → Docks → Shorts**

That link always points to the latest Windows installer.

## Manual zip install

[VerticalShortsPlugin-1.0.0-Windows.zip](https://github.com/PFS2689/vertical-shorts-plugin/releases/latest/download/VerticalShortsPlugin-1.0.0-Windows.zip) — copy `obs-shorts-vertical` into `%APPDATA%\obs-studio\plugins`.

## Features

- Vertical canvas presets: **1080×1920**, **720×1280**, **1080×1350**, plus custom sizes
- Live preview dock with selection outline and resize handles
- Drag to move camera / sources; corner & edge handles to resize (like main OBS)
- Numeric transform panel (X/Y, width/height, rotation)
- Fit / Stretch / Center / Reset transform actions
- Add camera or any existing OBS video source into a short scene
- Multiple short scenes
- Separate vertical recording output
- Scene layout saved with your OBS collection

## Requirements

- **Windows 10/11 64-bit**
- OBS Studio **30+** (31+ recommended)

## Build from source

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
cmake --install build_x64 --prefix release/RelWithDebInfo --config RelWithDebInfo
```

CI builds the Windows zip + Setup.exe when you push a version tag (for example `1.0.0`).

## License

GPL-2.0-or-later (same as OBS Studio).
