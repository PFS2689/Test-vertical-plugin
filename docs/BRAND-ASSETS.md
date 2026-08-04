# Brand Assets & Trademark Notice — Vertical Shorts Plugin

**Date obtained:** 2026-08-04  
**Plugin version:** 1.4.0

## Platform names and logos

YouTube, Twitch, TikTok, Instagram, and related names and marks are trademarks of their respective owners. Vertical Shorts is an independent OBS Studio plugin and is **not** affiliated with, endorsed by, or sponsored by YouTube, Google, Twitch, Amazon, TikTok, ByteDance, Instagram, Meta, or any other platform listed in the destination selector.

## Bundled destination icons

| File | Description | Source | License / redistribution |
|------|-------------|--------|--------------------------|
| `data/icons/youtube.png` / `.svg` | Original simplified play-button mark (red rounded rectangle + white triangle) | Created for this project | Original work; **not** the official YouTube logo |
| `data/icons/twitch.png` / `.svg` | Original simplified purple mark | Created for this project | Original work; **not** the official Twitch logo |
| `data/icons/tiktok.png` / `.svg` | Original simplified dark mark with accent bars | Created for this project | Original work; **not** the official TikTok logo |
| `data/icons/instagram.png` / `.svg` | Original simplified camera-style mark | Created for this project | Original work; **not** the official Instagram logo |
| `data/icons/custom-rtmp.png` / `.svg` | Neutral network / server glyph | Created for this project | Original work |

### Why original icons instead of official logos

Official brand logo redistribution is restricted by many platform brand guidelines. This plugin therefore ships **original, redistributable** icons that remain recognizable beside the platform **name in text**. Platform names are always shown next to icons; icons are never the sole indicator.

### Asset rules followed

- No scraping of logos from websites
- No hotlinking of remote logo files
- No downloading of logos while the plugin is running
- Bundled assets are static PNG and SVG image resources only
- Icons have transparent backgrounds, consistent sizing (48×48 PNG), and are suitable for light and dark OBS themes
- Vertical Shorts branding is not placed inside or over platform icons
- Official logos are not altered (they are not bundled)

## Required user setup (platforms)

Users must obtain livestream server URLs and stream keys from each platform’s official dashboard when available:

- **YouTube:** YouTube Studio livestream / stream settings (vertical stream key is separate from horizontal when configured that way)
- **Twitch:** Creator Dashboard → Stream key
- **TikTok:** TikTok LIVE Studio / RTMP access when the account is eligible
- **Instagram:** Supported external streaming / stream-key features when available for the account
- **Custom RTMP:** Any user-operated or third-party RTMP/RTMPS ingest

Eligibility for RTMP access varies by account, region, and platform policy. This plugin does not invent undocumented endpoints and does not claim every account has stream-key access.
