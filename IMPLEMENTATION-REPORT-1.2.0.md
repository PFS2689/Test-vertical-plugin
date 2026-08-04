# Vertical Shorts 1.2.0 — Implementation Report

## Summary

Removed the Vertical/Horizontal layout selector so the plugin is a dedicated vertical workspace. Added independent short/long clip controls sharing one vertical `replay_buffer`, Vertical Recording Automation (disabled by default), settings tabs, OBS hotkeys, clearer filenames, and validation/persistence for the new options.

## Modified files

- `buildspec.json` (1.2.0)
- `CMakeLists.txt`
- `README.md`
- `data/locale/en-US.ini`
- `src/plugin-main.cpp`
- `src/plugin-settings.hpp`
- `src/settings-dialog.cpp` / `.hpp`
- `src/shorts-dock.cpp` / `.hpp`
- `src/vertical-outputs.cpp` / `.hpp`
- `tests/test_settings.cpp`

## New files

- `src/recording-automation.cpp` / `.hpp`
- `IMPLEMENTATION-REPORT-1.2.0.md` (this file)

## Removed

- Layout selector UI and `WorkspaceLayout` setting/enum (no longer selectable; vertical-only)

## Layout selector removal

`workspaceCombo` and `ApplyWorkspaceLayout` were removed. The dock always uses vertical mirror scenes via `RefreshVerticalWorkspace`. Main OBS scene transforms are not overwritten by vertical edits.

## Workspace differentiation

| Main OBS | Vertical Shorts |
|---|---|
| Horizontal canvas / scenes / stream / record | Vertical Canvas / Vertical Streaming / Vertical Recording / Vertical Clips |
| Unchanged control names | Plugin controls labeled Go Live / Record / Short Clip / Long Clip |

## Short clips (📸)

Uses the shared vertical replay buffer. Duration presets: 10/20/30/60s + custom. Independent of long-clip settings.

## Long clips (📷)

Saves video (not stills) for 2/3/4/5 minutes or custom MM:SS / minutes+seconds. If the buffer is short of the requested duration, the UI reports available vs requested and offers to save the available portion.

## Shared buffer

One `replay_buffer` sized to `max(short, long)` capped at **900 seconds (15 minutes)**. Changing duration may prompt a buffer restart so capacity can be updated without silently discarding an active buffer.

## Custom duration validation

Rejects zero/negative/non-numeric/malformed MM:SS values and values above 900s. Warns at ≥300 seconds about memory/disk/encoder cost.

## Recording automation

Master toggle **off by default**. Status shown in Settings (and a small dock indicator when relevant).

### Start triggers

- Main OBS streaming starts
- Selected scene becomes active
- OBS finishes loading
- Scheduled local date/time
- Countdown finishes
- Vertical Shorts live starts

### Stop triggers

- Main OBS streaming stops
- Trigger scene inactive
- Configured duration reached
- Scheduled end time
- Vertical Shorts live stops
- OBS shutting down

### Schedules

Stored as local `yyyy-MM-dd` / `HH:mm` strings plus repeat mode (once/daily/weekly/weekdays). Uses system time zone. Requires OBS open + plugin loaded (no background service).

### Scene triggers

Scene UUID + name from the active collection; list refreshes on collection/scene changes; missing scenes are handled without crashing.

### Manual vs automatic

- Auto-start is a no-op if vertical recording is already active
- Manual start marks recording as manual (automation will not start a second output)
- Manual stop during automation can confirm when enabled
- Automation never stops main OBS recording

## OBS settings inheritance

Encoders, bitrates, and related output settings are copied from the main OBS streaming/recording outputs when available; otherwise safe defaults. Main OBS settings are not modified.

## Settings storage

Persisted in the OBS scene collection save blob under `obs-shorts-vertical` (including hotkey bindings arrays).

## Security / antivirus

- No Defender bypasses, exclusions, injection, packing, hidden services, or admin elevation
- Custom Win32 installer path retained from 1.1.0 (no Inno Setup)
- Official OBS APIs only for outputs/hotkeys
- No new third-party download dependencies

## Remaining limitations

- Buffer “available seconds” is estimated from wall-clock time since buffer start (OBS does not expose exact buffered duration)
- Vertical live and main OBS stream may contend for the same configured service
- Full in-OBS integration tests require a Windows OBS host (CI builds the Release plugin)
- Weekday bitmask UI for schedule repeat is stored; full per-day checkbox UI is minimal (mask defaults allow all when zero)

## Build / install / test

See README. Windows Release via GitHub Actions. Unit tests: `-DVSP_BUILD_TESTS=ON` → `test_settings`.
