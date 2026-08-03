# OBS Shorts Vertical

Vertical canvas plugin for [OBS Studio](https://obsproject.com) made for **YouTube Shorts**, **TikTok**, and **Instagram Reels**.

Adds a dedicated 9:16 (or custom) shorts canvas with the same kind of **move and resize** controls you use on the main OBS preview: drag sources to reposition, and use corner/edge handles to scale.

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

- OBS Studio **28+** (30+ recommended)
- Qt 6
- Platform: Windows, macOS, or Linux

## Install (prebuilt)

1. Download a release for your OS.
2. Copy the plugin binary into your OBS plugins folder and the `data` folder beside it:
   - **Windows:** `C:\Program Files\obs-studio\obs-plugins\64bit\` and `...\data\obs-plugins\obs-shorts-vertical\`
   - **macOS:** `~/Library/Application Support/obs-studio/plugins/obs-shorts-vertical/`
   - **Linux:** `~/.config/obs-studio/plugins/obs-shorts-vertical/` (or system `lib/obs-plugins`)
3. Restart OBS.
4. Open **View → Docks → Shorts**.

## Build

### Dependencies

- CMake 3.16+
- OBS Studio development files (`libobs`, `obs-frontend-api`)
- Qt 6 (Widgets)

### Configure & compile

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH="/path/to/Qt6;/path/to/obs-studio-install"
cmake --build build
cmake --install build --prefix "$HOME/.config/obs-studio"
```

If you build OBS from source, point CMake at that build/install prefix so `find_package(libobs)` and `find_package(obs-frontend-api)` succeed.

### In-tree (optional)

Copy this repo into `UI/frontend-plugins/obs-shorts-vertical` (or `frontend/plugins` on newer OBS trees) and add:

```cmake
add_subdirectory(obs-shorts-vertical)
```

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

Tip: keep your normal landscape stream on the main canvas and compose Shorts/TikTok takes in this dock at the same time.

## License

GPL-2.0-or-later (same as OBS Studio).  
Qt display helpers adapted from OBS Studio / community plugin patterns.
