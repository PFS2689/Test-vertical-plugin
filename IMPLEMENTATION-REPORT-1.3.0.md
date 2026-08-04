# Vertical Shorts 1.3.0 — Limitations Fix Report

## Former limitations and outcomes

### 1. Dual streaming / same destination
**Mitigated with detection + safe alternate destinations (platform restriction remains).**

- Before start, compares main vs vertical service type/server/key.
- If main stream is active and destinations conflict → blocks start with a clear resolution message.
- Settings: inherit main / separate vertical key / custom server+key.
- Test Destination validates without going live.
- Stream keys never logged unmasked (`MaskSecret` / `SanitizeUrlForLog`).
- Distinct keys or servers are allowed on a separate isolated output instance.
- **External:** platforms still reject two encoders on one key; the plugin cannot bypass that.

### 2. Audio mixer only volume/mute
**Fixed in code (real OBS meters + advanced controls).**

- `AudioMixerPanel` uses `obs_volmeter_create` / attach / callback.
- Per-source: name, mute, volume slider, dB label, peak readout, painted meter.
- Context menu: Filters, Properties (OBS frontend API), Advanced Audio dialog (balance, sync offset, mono when available, monitoring, track routing), Rename.
- Meter updates marshalled to the Qt UI thread; callbacks removed on clear/shutdown.

### 3. Clip buffer multi-second startup lag / readiness
**Fixed / clarified.**

- Removed the artificial 1500 ms delay; buffer starts on dock init / OBS finished loading when “Keep clip buffer ready” is on (default).
- Also starts with Vertical Live / Vertical Recording when those options are enabled.
- Status: stopped / starting / buffering / ready short / ready long / error + available seconds.
- Partial-duration prompt when requested > available; buffer stays running after saves.
- **Unavoidable pre-roll:** only footage after buffer start can be saved (documented as normal).

### 4. Tests require manual `-DVSP_BUILD_TESTS=ON`
**Fixed.**

- `include(CTest)` + `BUILD_TESTING` / `VSP_BUILD_TESTS`.
- Presets: `windows-debug`, `windows-release`, `windows-tests`, CI enables tests.
- CI runs test targets + `ctest --output-on-failure` before packaging; guards against test binary leaks into `release/`.

### 5. Obsolete Vertical/Horizontal layout instructions
**Fixed.**

- Selector already removed in 1.2.0; docs/install/README updated to vertical-only install/test steps; version strings set to 1.3.0.

## Modified / new / removed files

**New:** `src/stream-destination.hpp`, `src/audio-mixer-panel.*`, `tests/test_stream_destination.cpp`, `tests/test_mixer_model.cpp`, this report  
**Modified:** settings, outputs, dock, settings dialog, CMake, presets, CI build script, locale, README, INSTALL-WINDOWS, buildspec (1.3.0), tests  
**Removed:** none (layout selector already gone)

## Security

- No Defender bypasses, packing, injection, hidden services, or admin elevation.
- Credentials: password echo on key field; masked in logs/messages.
- Release packaging still excludes test binaries (CI guard).

## Build / test results

- Local unit tests: settings, stream_destination, mixer_model — all passed.
- Windows CI: run after push (RelWithDebInfo + ctest + package).

## Remaining external limitations

- Same stream key + same ingest while main OBS is live is blocked by design; platforms still enforce this.
- Exact buffered media duration is estimated from wall-clock since buffer start (OBS replay buffer does not expose a precise API).
- Full end-to-end OBS UI verification requires a Windows OBS host.
