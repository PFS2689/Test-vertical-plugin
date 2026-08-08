# Windows installer (Inno Setup)

Vertical Shorts Plugin ships a **standard Inno Setup 6** installer with a permanent
in-place upgrade identity.

## Final installer name

```
Vertical Shorts Plugin 1.0.5 Setup.exe
```

(Product name and version come from `buildspec.json`.)

Release/build timestamps are generated at CI configure/package/publish time (`PLUGIN_BUILD_TIMESTAMP`, `PackageTimestampUtc`, GitHub Release **Last Updated**). They must never be hard-coded or copied from an older 1.0.5 artifact.

## Permanent AppId (do not change)

```
{D4336EAC-D873-4E6B-8575-07096987E0C8}
```

Source of truth: `buildspec.json` → `uuids.windowsApp` → `installer/windows/VerticalShortsPlugin.iss` → `AppId`.

## Install location (OBS root)

`{app}` is the **OBS Studio installation directory**, defaulting to:

```
{autopf}\obs-studio
→ C:\Program Files\obs-studio
```

Plugin destinations:

```
{app}\obs-plugins\64bit\obs-shorts-vertical.dll
{app}\data\obs-plugins\obs-shorts-vertical\
```

Administrator (UAC) is required.

### `{app}` initialization rule

`ExpandConstant('{app}')` must **never** run inside `InitializeSetup` (or any code path that runs before the install directory is initialized). Doing so raises:

```
Internal error: An attempt was made to expand the "{app}" constant before it was initialized.
```

Early logic uses `{autopf}`, registry lookups, and `GObsInstallPath` instead.

## OBS detection

Before the wizard starts, Setup looks for `bin\64bit\obs64.exe` via:

1. Previous Vertical Shorts uninstall `InstallLocation` (if it is a valid OBS root)
2. `HKLM\Software\OBS Studio`
3. OBS Studio uninstall `InstallLocation`
4. `{autopf}\obs-studio` / `{pf}\obs-studio`

If OBS is not found, the directory page lets the user browse manually.

## In-place upgrades

When Vertical Shorts Plugin is already installed, running a newer Setup.exe:

1. Detects the previous version (uninstall registry + known DLL paths — not `{app}`)
2. Shows an **Upgrade** / **Cancel** confirmation
3. Requires OBS Studio to be closed
4. Creates a lightweight backup under `%LOCALAPPDATA%\VerticalShortsPlugin\upgrade-backups\`
5. Replaces plugin binaries/resources under the OBS tree
6. Removes legacy ProgramData plugin copies if present
7. Preserves user configuration (scene collection + Credential Manager)

## Build order (required)

1. Build the plugin (**Release** on tags, RelWithDebInfo on PRs)
2. Verify `obs-shorts-vertical.dll` + locale + `INSTALL.txt`
3. Stage payload under `release/staging/obs-shorts-vertical/` (includes `install-meta.ini`)
4. Compile `installer/windows/VerticalShortsPlugin.iss` with **ISCC.exe**
5. Scan / sign / publish the resulting Setup.exe

## Local packaging (Windows)

```powershell
choco install innosetup --no-progress -y
$env:CI = '1'
.\.github\scripts\Package-Windows.ps1 -Target x64 -Configuration Release
```

Output:

- `release\Vertical Shorts Plugin <version> Setup.exe`
- `release\Vertical-Shorts-Plugin-<version>.zip`

## Script

- `installer/windows/VerticalShortsPlugin.iss` — Inno Setup 6 project
- AppId GUID: `buildspec.json` → `uuids.windowsApp`
- Compression: `lzma2/max`, solid
