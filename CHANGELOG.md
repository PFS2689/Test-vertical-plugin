# Changelog — Vertical Shorts Plugin

**Official product version: 1.0.5**

All shipping artifacts (plugin DLL, Setup.exe, documentation, and installer UI) use **1.0.5** only.

Earlier intermediate development labels (1.1.x–1.4.x) used during feature work are **not** separate official releases. Their notes are preserved below as development history only.

---

## 1.0.5 (official)

Current official release of Vertical Shorts Plugin for OBS Studio (Windows).

**Product description:** Professional Vertical Streaming Plugin for OBS Studio

Includes:

- Vertical-only production dock (scenes, sources, mixer, transitions)
- Independent Vertical Streaming Destination (YouTube / Twitch / TikTok / Instagram / Custom RTMP)
- Custom platform selector with bundled SVG logos (embedded in the DLL)
- Secure credential storage (Windows Credential Manager + DPAPI fallback)
- Vertical recording, short/long clips, clip buffer readiness
- Optional recording automation and hotkeys
- Custom per-user Setup.exe (no Inno Setup / NSIS)
- MSVC Release build (`/MD`, `/DEBUG:NONE`), Windows Defender + ClamAV gates, SHA-256 checksums
- Authenticode signing required for tag releases (Azure Artifact Signing or OV/EV PFX)

---

## Development history (pre-official consolidation)

These entries document work that was folded into official **1.0.5**. They are not alternate product versions.

### Feature work later labeled 1.4.x during development

- Independent Vertical Streaming Destination (never inherits main OBS stream)
- Platform presets, status card, Test Configuration, masked stream keys
- Polished custom logo selector and hero header
- Security/release audit hardening (DPAPI fallback, scrub legacy keys, release allowlists)

### Feature work later labeled 1.3.x during development

- Dual-stream conflict handling (superseded by fully independent destinations)
- Real OBS volmeter mixer
- Clip buffer auto-start / readiness status
- CTest integration in CI

### Feature work later labeled 1.2.x during development

- Long clips, recording automation, vertical-only workspace
- Go Live / Record / Short Clip / Long Clip / Settings controls

### Feature work later labeled 1.1.x / 1.0.x during development

- Vertical Shorts dock UI redesign
- Custom MSVC Setup.exe replacing Inno Setup
- Defender false-positive mitigations for the installer stub
