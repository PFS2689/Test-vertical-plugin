# Vertical Shorts 1.4.0 — Independent Vertical Streaming Destination

## Summary

Vertical Go Live now uses a **completely independent** streaming destination. It does not inherit, copy, or reuse the main OBS streaming service, server, account, or stream key. Main OBS livestream and Vertical Shorts livestream remain separate outputs.

## Destination selector (Settings popup only)

**Settings → Vertical Streaming → Vertical Streaming Destination**

1. Destination dropdown with icon + platform name (YouTube, Twitch, TikTok, Instagram, Custom RTMP Server)
2. Destination status card (safe fields only)
3. Server URL
4. Stream key (masked) + Show/Hide Key
5. Test Configuration / Save Destination / Clear Vertical Credentials
6. Platform-specific help text + instructions button
7. Saved destinations: Add / Rename / Delete (multiple named destinations)

Selection persists across OBS restarts (metadata in plugin settings; secrets in OS credential storage).

## Platform presets

| Platform | Fields | Notes |
|----------|--------|-------|
| YouTube | Server URL, stream key, destination name | Suggested RTMPS `rtmps://a.rtmp.youtube.com/live2` only; user must paste vertical key from YouTube Studio |
| Twitch | Ingest selector (auto/manual), stream key, name | Public RTMPS ingest list; user supplies Twitch stream key |
| TikTok | Server URL, stream key, name | No invented universal URL; eligibility varies |
| Instagram | Server URL, stream key, name | No undocumented endpoint; eligibility varies |
| Custom RTMP | Name, server URL, key, optional user/pass, protocol indicator | RTMP/RTMPS only; malformed/unsupported rejected |

Switching platforms loads that platform’s previously saved Vertical Shorts settings and does not copy keys across platforms.

## Credentials & stream-key protection

- Windows: Windows Credential Manager (`CredWriteW` / `CredReadW` / `CredDeleteW`)
- Fallback: restricted local file under AppLocalData with warning (never claimed as secure)
- Plugin config stores destination metadata only (platform, name, server hostname URL, flags) — **never** stream keys/passwords in the OBS save blob
- Key field masked by default; re-masked when Settings closes
- Keys never logged, never placed in error dialogs, tooltips, or diagnostics as full values
- Clear Vertical Credentials confirms and removes only the selected Vertical Shorts destination secrets

## Test Configuration

Validates local configuration (fields, protocol, URL structure) and that an independent `rtmp_custom` service can be created. Does **not** go live and does **not** claim credentials are verified by the platform.

## Vertical Go Live

- Creates an independent vertical output + owned `rtmp_custom` service from the active Vertical Shorts destination
- Never calls `obs_frontend_get_streaming_service()` for the vertical destination
- Does not stop/restart/modify the main OBS stream, service, or key
- Status: Connecting / Live / Reconnecting / Stopping / Offline / Error
- Validation failure offers **Open Streaming Settings**

## Logos

Original redistributable PNG/SVG icons (not official brand marks). See `docs/BRAND-ASSETS.md`.

## Files

**New:** `src/credential-store.*`, `src/vsp-resources.qrc`, `data/icons/*`, `docs/BRAND-ASSETS.md`, `tests/test_credential_store.cpp`, this report  

**Modified:** `stream-destination.hpp`, `plugin-settings.hpp`, `settings-dialog.*`, `vertical-outputs.*`, `shorts-dock.*`, `CMakeLists.txt`, locale, README, INSTALL-WINDOWS, buildspec (1.4.0), stream/settings/mixer tests  

**Removed:** inherit-main / separate-key destination modes (replaced by independent platform destinations)

## Tests added/updated

- Platform selection, icon paths, missing-logo fallback, bundled asset presence
- Destination save/load model, platform switching, separate credentials
- Masked secrets / sanitization (keys never in log helpers)
- Invalid URL / unsupported protocol / missing key
- Custom RTMP validation
- Credential store save/load/clear (fallback on non-Windows)
- Settings defaults no longer expect inherit-main

## Security review

- No runtime logo downloads, no Defender exclusions, no packing/obfuscation, no PowerShell credential hacks, no bundled executables beyond the existing installer, no background services
- Build Release/RelWithDebInfo via existing Windows CI; ClamAV release gate unchanged

## Build / test results

- Local unit tests (Linux/Qt): settings, stream_destination, mixer_model, credential_store — all passed
- Windows CI (RelWithDebInfo + `vsp_tests` + ctest): **passed** on `cursor/independent-stream-dest-11e0`
- ClamAV: unchanged release packaging gate (runs on published releases; no new executables or runtime downloads added)

- Users must obtain livestream credentials from each platform; not every account has RTMP access
- Test Configuration cannot prove a stream key is accepted without going live
- Encoder quality settings may still mirror compatible OBS encoder templates; destination/credentials do not
- Full GUI verification requires a Windows OBS host

## Manual setup users must complete

1. Open each platform’s official livestream dashboard
2. Copy the **vertical** server URL and stream key intended for Vertical Shorts
3. Paste into Settings → Vertical Streaming Destination
4. Save Destination, optionally Test Configuration, then Go Live
