# Vertical Shorts Plugin for OBS Studio

**Version 1.0.5** — Windows

Vertical canvas plugin for [OBS Studio](https://obsproject.com) made for **YouTube Shorts**, **TikTok**, and **Instagram Reels**.

## Download

- **Installer:** [Vertical-Shorts-Plugin-Setup.exe](https://github.com/PFS2689/verticalshorts/releases/latest/download/Vertical-Shorts-Plugin-Setup.exe)
- **Manual zip:** [Vertical-Shorts-Plugin.zip](https://github.com/PFS2689/verticalshorts/releases/latest/download/Vertical-Shorts-Plugin.zip)

Every release is scanned with **ClamAV** in CI before publishing. Malicious packages are blocked.

If Windows SmartScreen shows **Unknown publisher**, choose **More info → Run anyway**. That warning appears because this open-source build is not Authenticode-signed — it is not a ClamAV malware hit.

## Install (Setup.exe)

1. Close OBS Studio  
2. Run `Vertical-Shorts-Plugin-Setup.exe`  
3. Start OBS → **View → Docks → Vertical Shorts**

## Install (zip)

1. Close OBS Studio  
2. Open `%APPDATA%\obs-studio\plugins`  
3. Copy the `obs-shorts-vertical` folder from the zip into that folder  
4. Start OBS → **View → Docks → Vertical Shorts**

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

## License

GPL-2.0-or-later (same as OBS Studio). Full source is in this repository.
