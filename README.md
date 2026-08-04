# Vertical Shorts Plugin for OBS Studio

**Version 1.4.0** — Windows

Vertical production dock for [OBS Studio](https://obsproject.com): Shorts, TikTok, Reels, and Twitch vertical.

## Download

- **Setup.exe:** [Vertical-Shorts-Plugin-Setup.exe](https://github.com/PFS2689/verticalshorts/releases/latest/download/Vertical-Shorts-Plugin-Setup.exe)
- **Zip:** [Vertical-Shorts-Plugin.zip](https://github.com/PFS2689/verticalshorts/releases/latest/download/Vertical-Shorts-Plugin.zip)

## UI overview

Dock title: **Vertical Shorts**

- Vertical-only workspace (main OBS remains the horizontal production canvas)
- OBS-style panels: Scenes, Sources, Audio Mixer (real meters), Transitions
- Canvas controls (bottom-right): 🟢 Go Live · ⏺️ Record · 📸 Short Clip · 📷 Long Clip · ⚙️ Settings
- Vertical transforms are stored on private mirror scenes so your main OBS canvas is not overwritten
- **Independent Vertical Streaming Destination** (YouTube / Twitch / TikTok / Instagram / Custom RTMP) — never inherits the main OBS stream key or service
- Clip-buffer readiness status, recording automation, and hotkeys

## Install / test

1. Install the current Setup.exe or zip release.
2. Open OBS.
3. Go to **View → Docks → Vertical Shorts**.
4. Open **⚙️ Settings → Vertical Streaming** and configure a Vertical Streaming Destination with a vertical-only server URL and stream key from the platform dashboard.
5. Confirm the main OBS canvas and main stream settings remain unchanged.
6. Test Vertical Go Live, Vertical Record, Short Clip, Long Clip, and Settings.
7. Confirm output files save to the selected vertical recording path.

## Requirements

- Windows 10/11 64-bit  
- OBS Studio **31+** (built against 31.1.1)

## Build

```bash
# Windows (VS 2022)
cmake --preset windows-release
cmake --build --preset windows-release

# Unit tests
cmake --preset windows-tests
cmake --build --preset windows-tests
ctest --preset windows-tests
```

CI configures tests automatically and runs `ctest --output-on-failure` before packaging. Test binaries are not included in the release zip/Setup.exe.

## License

GPL-2.0-or-later (same as OBS Studio).
