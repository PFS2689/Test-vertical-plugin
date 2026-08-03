# Shorts Vertical for OBS Studio

**Version 1.0.0** — Windows release

Vertical canvas plugin for [OBS Studio](https://obsproject.com) made for **YouTube Shorts**, **TikTok**, and **Instagram Reels**.

Adds a dedicated 9:16 (or custom) shorts canvas with the same kind of **move and resize** controls you use on the main OBS preview: drag sources to reposition, and use corner/edge handles to scale.

## Download (Windows)

Grab the latest Windows zip from [Releases](https://github.com/PFS2689/Test-vertical-plugin/releases):

- **`ShortsVertical-1.0.0-Windows.zip`**

### Install

1. Close OBS Studio.
2. Press `Win+R`, paste `%APPDATA%\obs-studio\plugins`, press Enter (create the folder if needed).
3. Copy the `obs-shorts-vertical` folder from the zip into that plugins folder.
4. Start OBS → **View → Docks → Shorts**.

Full steps are also in `INSTALL.txt` inside the zip.

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

### Dependencies

- CMake 3.28+
- Visual Studio 2022 (Windows)
- OBS plugin build dependencies (fetched automatically via `buildspec.json`)

### Windows

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64
cmake --install build_x64 --prefix release/RelWithDebInfo --config RelWithDebInfo
```

CI builds and publishes the Windows zip when you push a version tag such as `1.0.0`.

## How to use

1. Open the **Shorts** dock.
2. Pick a canvas size (default 1080×1920).
3. Click **Add Camera** (or **Add Source** to reuse something from your main scenes).
4. In the preview:
   - **Click** a source to select it
   - **Drag** to move it
   - Drag a **corner or edge handle** to resize
5. Fine-tune with the Transform fields, or use Fit / Stretch / Center.
6. Hit **Record** to capture only the vertical canvas.

## License

GPL-2.0-or-later (same as OBS Studio).
