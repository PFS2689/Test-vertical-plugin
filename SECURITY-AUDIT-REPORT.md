# Security, Antivirus, and Release Audit — Vertical Shorts Plugin

**Audit date:** 2026-08-04  
**Branch:** `cursor/independent-stream-dest-11e0`  
**Plugin version:** 1.0.5  

This audit covers C++/Qt/OBS sources, CMake, CI/workflows, the custom Windows Setup.exe, packaging scripts, resources, and credential handling.  
**No antivirus bypass, Defender exclusions, packers, or obfuscation were added.**

---

## 1. Security issues found

### Critical
- None.

### High (pre-fix)
| ID | Issue |
|----|--------|
| H1 | Credential Manager failure fell back to **plaintext** hidden file; Windows ACL not applied; UI overstated “restricted” |
| H2 | Windows Setup.exe / plugin DLL are **unsigned** (SmartScreen “Unknown publisher”) — supply-chain / reputation risk |

### Medium (pre-fix)
| ID | Issue |
|----|--------|
| M1 | Legacy `vertical_stream_key` could remain in OBS scene JSON until next save |
| M2 | Unencrypted `rtmp://` accepted without warning |
| M3 | `MaskSecret` kept last 4 characters by default (risk if reused in logs) |
| M4 | Setup embeds DLL as RCDATA (legitimate; known AV ML heuristic) |
| M5 | OBS stream last-error text shown raw in UI (could contain URL/userinfo) |
| M6 | Release `freshclam \|\| true` could publish with stale ClamAV signatures |
| M7 | No machine-readable `SHA256SUMS` asset; tag not checked against `buildspec.json` |

### Low / Info
- Recording path fully user-controlled (expected); filename sanitization incomplete for exotic chars
- RTMP username stored in settings blob (non-secret metadata)
- Secrets in process memory as `QString` (normal for OBS/Qt)
- Floating GitHub Action major tags (not commit SHAs)
- Redundant `data/icons` on disk + Qt resources

### Explicitly **not** present
- Process injection, packers/UPX, Defender exclusions, hidden PowerShell, remote binary download/exec
- Admin elevation (`asInvoker` only)
- Persistence (Run keys / services / scheduled tasks)
- Runtime logo/network downloads
- Test binaries installed into the plugin package (CI leak guard + release allowlist)

---

## 2. Security issues fixed (this audit)

| Fix | Detail |
|-----|--------|
| H1 | Windows fallback now **DPAPI-protects** secrets (`CryptProtectData`); directory DACL best-effort restricted to current user; conservative filename sanitization; clearer UI warning |
| M1 | `obs_data_erase` for `vertical_stream_key` / legacy stream fields **immediately on load** |
| M2 | Warning emitted for unencrypted `rtmp://` during URL validation |
| M3 | `MaskSecret` defaults to **full** mask; optional keepTail retained for tests |
| M5 | Stream start errors sanitized via `SanitizeUserFacingError` before UI display |
| Path/filename | Stronger `SanitizeFilenamePart`; `ValidateRecordingPath`; outputs resolved under cleaned absolute recording directory |
| M6/M7 | Release workflow: **fail if freshclam fails**; zip path allowlist + reject `test_*`; emit `SHA256SUMS.txt`; tag (`vX.Y.Z`) must match `buildspec.json` version; publish only Setup.exe + zip + checksums |

---

## 3. Dependencies removed
- None required for security. No unused runtime DLLs are bundled (plugin-only MODULE).

## 4. Dependencies updated
- No third-party library version bumps in this pass.
- OBS / prebuilt / Qt6 remain **pinned with SHA-256** in `buildspec.json` (obs-studio 31.1.1, deps/Qt 2025-07-11).

---

## 5. Antivirus scan results

| Scanner | Environment | Result |
|---------|-------------|--------|
| **ClamAV** | GitHub Actions release job (`release-windows.yaml`) | Gate remains: infected → abort; **freshclam must succeed**; report published as `clamav-report.txt` |
| **Windows Defender** | Not runnable in this Linux cloud agent | **Not executed in this audit environment.** CI does not currently run MpCmdRun. Manual recommendation below. |
| **Local Linux unit tests** | This agent | Passed after audit fixes (settings / stream destination / credential store / mixer) |

**Honesty statement:** This audit **cannot** claim the project is “virus-free.” Unsigned Setup.exe that embeds a PE as RCDATA can still receive Defender ML / SmartScreen reputation detections. Those must be investigated per-file if reported — never suppressed.

### Suspected false-positive profile (document, do not ignore)
- **File:** `Vertical-Shorts-Plugin-Setup.exe`
- **Likely cause:** Small custom installer extracting embedded DLL (RCDATA) — common ML heuristic; not Inno/NSIS
- **Mitigation:** Authenticode signing; keep payload minimal; submit to Microsoft WDSI if FP confirmed after signing

---

## 6. Installer review

| Check | Status |
|-------|--------|
| Per-user `%APPDATA%\obs-studio\plugins\obs-shorts-vertical\` only | Pass |
| Files: DLL + `en-US.ini` + `INSTALL.txt` only | Pass |
| No admin (`asInvoker`) | Pass |
| No network / no child processes / no Run keys | Pass |
| No Defender exclusions | Pass |
| `/DYNAMICBASE` `/NXCOMPAT` on Setup link | Pass (Package-Windows.ps1) |
| Authenticode signed | **Fail / not configured** (remaining risk H2) |

---

## 7. Release package review

| Check | Status |
|-------|--------|
| Zip contains plugin tree under `obs-shorts-vertical/` | Pass (allowlisted) |
| No nested `.exe` / scripts / `test_*` | Pass (release gate) |
| PDBs stripped before zip | Pass |
| No Qt/OBS runtime DLLs bundled | Pass |
| SHA-256 in release notes + `SHA256SUMS` asset | Pass (after this change) |
| ClamAV before publish | Pass (hardened freshclam) |

---

## 8. Remaining risks

1. **Unsigned binaries** → SmartScreen / reputation FPs until Authenticode is configured.  
2. **Setup RCDATA dropper shape** → may still trip ML even when clean.  
3. **Plain RTMP** still allowed (with warning) — network exposure of stream key if user chooses it.  
4. **Secrets in RAM** while OBS is running (industry-normal for streaming apps).  
5. **This environment could not run MSVC Release or Windows Defender** — Windows CI RelWithDebInfo/Release + ClamAV on tags remain the authoritative gates.  
6. GitHub Actions not pinned to commit SHAs.

---

## 9. Recommendations before public release

1. **Configure Authenticode** (Azure Trusted Signing or EV cert) for `obs-shorts-vertical.dll` and `Vertical-Shorts-Plugin-Setup.exe`; re-scan with Defender after signing.  
2. On a Windows release host, run:  
   `MpCmdRun.exe -Scan -ScanType 3 -File <artifact>`  
   and keep the log; do **not** add exclusions.  
3. Publish only from GitHub tag releases so ClamAV + `SHA256SUMS` gates apply.  
4. Pin Actions to full commit SHAs.  
5. Optionally add a Windows CI Defender scan job (still no exclusions).  
6. If Defender flags Setup.exe after signing, open a Microsoft false-positive submission with SHA-256 + source URL — do not instruct users to disable AV.

---

## Verification performed in this audit pass

- Static review of all `src/**`, `tests/**`, `.github/**`, `cmake/**`, installer, resources  
- Grep for process exec, injection, AV evasion, runtime downloads — **clean in runtime code**  
- Hardening commits listed above  
- Local Qt unit tests re-run after changes  

**Release MSVC build + Defender:** defer to Windows GitHub Actions / maintainer machine; cloud agent is Linux-only.
