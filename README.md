# Vertical Shorts Plugin for OBS Studio

**Version 1.0.5** — Windows

Professional Vertical Streaming Plugin for OBS Studio — vertical production dock for Shorts, TikTok, Reels, and Twitch.

## Download

- **Setup.exe:** [Vertical-Shorts-Plugin-1.0.5-Setup.exe](https://github.com/PFS2689/verticalshorts/releases/latest/download/Vertical-Shorts-Plugin-1.0.5-Setup.exe)
- **Zip:** [Vertical-Shorts-Plugin-1.0.5.zip](https://github.com/PFS2689/verticalshorts/releases/latest/download/Vertical-Shorts-Plugin-1.0.5.zip)
- **Checksums:** [SHA256SUMS.txt](https://github.com/PFS2689/verticalshorts/releases/latest/download/SHA256SUMS.txt)
- **Signing:** Production builds are Authenticode-signed (see [docs/SIGNING-WINDOWS.md](docs/SIGNING-WINDOWS.md))

## UI overview

Native OBS docks (View → Docks):

- **Vertical Shorts** — large vertical canvas + Go Live / Record / Short Clip / Long Clip / Settings
- **Vertical Scenes** — vertical-only scene list (does not change horizontal Scenes)
- **Vertical Sources** — sources/transform for the active vertical scene only
- **Vertical Transitions** — vertical transition type, duration, preview/trigger

Settings uses an OBS-style dialog (pages left, categories right, OK / Cancel / Apply) with General, Vertical Canvas, Vertical Recording, Vertical Clips, Recording Automation, Vertical Streaming, Audio, Hotkeys, Advanced, and About.

- **Independent Vertical Streaming Destination** (YouTube / Twitch / TikTok / Instagram / Custom RTMP) — never inherits the main OBS stream key or service
- Clip buffer, recording automation, and hotkeys

## Install / test

1. Install the current Setup.exe (UAC / Administrator) or zip release into  
   `%ProgramData%\obs-studio\plugins\obs-shorts-vertical\`  
   (OBS does **not** load plugins from `%APPDATA%\obs-studio\plugins` on Windows).
2. Open OBS Studio **32.2.1**.
3. If needed, enable **Vertical Shorts Plugin** in **Tools → Plugin Manager**, then restart OBS.
4. Open **View → Docks** and enable Vertical Shorts / Scenes / Sources / Transitions as needed.
5. Open **Settings → Vertical Streaming** and configure a Vertical Streaming Destination.
6. Confirm the main OBS canvas and main stream settings remain unchanged.
7. Test Vertical Go Live, Vertical Record, Short Clip, Long Clip, and Settings.
8. Confirm output files save to the selected vertical recording path.

## Requirements

- Windows 10/11 64-bit  
- OBS Studio **32.2.1** (built against 32.2.1 / obs-deps 2026-07-15)

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
