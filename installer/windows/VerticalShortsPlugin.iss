; Vertical Shorts Plugin — Inno Setup 6
;
; PERMANENT AppId (never change across versions):
;   {D4336EAC-D873-4E6B-8575-07096987E0C8}
; Same GUID as buildspec.json → uuids.windowsApp
;
; Every future release (1.0.5, 1.0.6, 1.1.0, 2.0.0, ...) MUST keep this AppId.
; Changing it breaks in-place upgrade detection.
;
; Install location (OBS 32.x third-party plugin load path):
;   %ProgramData%\obs-studio\plugins\obs-shorts-vertical\
;
; User configuration is NOT stored under {app}. It lives in the OBS scene
; collection ("obs-shorts-vertical") + Windows Credential Manager. Upgrades
; replace binaries/resources only and must never wipe user settings.

#ifndef MyAppName
  #define MyAppName "Vertical Shorts Plugin"
#endif
#ifndef MyAppVersion
  #define MyAppVersion "1.0.5"
#endif
#ifndef MyAppPublisher
  #define MyAppPublisher "Vertical Shorts Plugin Contributors"
#endif
#ifndef MyAppURL
  #define MyAppURL "https://github.com/PFS2689/verticalshorts"
#endif
#ifndef SourceDir
  #define SourceDir "..\..\release\staging"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\release"
#endif
#ifndef OutputBaseFilename
  #define OutputBaseFilename "Vertical Shorts Plugin 1.0.5 Setup"
#endif

; Permanent product identity — DO NOT regenerate when bumping MyAppVersion.
#define MyAppIdGuid "D4336EAC-D873-4E6B-8575-07096987E0C8"

[Setup]
; Double-brace escapes to a single brace in the compiled script → {GUID}
AppId={{{#MyAppIdGuid}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={commonappdata}\obs-studio\plugins\obs-shorts-vertical
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
DisableDirPage=yes
DirExistsWarning=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename={#OutputBaseFilename}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#MyAppName} {#MyAppVersion}
VersionInfoVersion={#MyAppVersion}.0
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription=Professional Vertical Streaming Plugin for OBS Studio
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}
VersionInfoCopyright=Copyright (C) Vertical Shorts Plugin Contributors
AllowNoIcons=yes
CloseApplications=no
RestartApplications=no
RestartIfNeededByRun=no
CreateUninstallRegKey=yes
UpdateUninstallLogAppName=yes
AllowCancelDuringInstall=yes
UsedUserAreasWarning=no
; No reboot required for plugin DLL replacement when OBS is closed.
AlwaysRestart=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; Binary + locale + INSTALL.txt + install-meta.ini from staged payload only.
; ignoreversion: always replace plugin files on upgrade (versioned by AppId, not file ver).
Source: "{#SourceDir}\obs-shorts-vertical\*"; DestDir: "{app}"; \
    Flags: ignoreversion recursesubdirs createallsubdirs uninsrestartdelete

[UninstallDelete]
; Remove obsolete Vertical Shorts-only leftovers under the plugin tree.
Type: filesandordirs; Name: "{app}\bin"
Type: filesandordirs; Name: "{app}\data"
Type: files; Name: "{app}\INSTALL.txt"
Type: files; Name: "{app}\install-meta.ini"

[Code]
const
  OBS_WINDOW_CLASS = 'OBSWindowClass';
  WM_CLOSE = $0010;

var
  GIsUpgrade: Boolean;
  GPreviousVersion: String;
  GUpgradeBackupDir: String;

function PluginInstallRoot: String;
var
  Canonical: String;
begin
  { Prefer the wizard/app dir; fall back to the canonical OBS 32 load path. }
  Canonical := ExpandConstant('{commonappdata}\obs-studio\plugins\obs-shorts-vertical');
  Result := ExpandConstant('{app}');
  if Result = '' then
    Result := Canonical
  else if (not DirExists(Result)) and DirExists(Canonical) then
    Result := Canonical;
end;

function InnoUninstallRegKey: String;
begin
  Result := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{' +
            '{#MyAppIdGuid}' + '}_is1';
end;

function LegacyUninstallRegKey: String;
begin
  { Pre-Inno custom MSVC installer used the bare product GUID (no _is1). }
  Result := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{' +
            '{#MyAppIdGuid}' + '}';
end;

function QueryUninstallString(const SubKey, ValueName: String; var OutValue: String): Boolean;
begin
  Result := False;
  OutValue := '';
  if RegQueryStringValue(HKLM64, SubKey, ValueName, OutValue) then begin
    Result := True;
    exit;
  end;
  if RegQueryStringValue(HKLM, SubKey, ValueName, OutValue) then begin
    Result := True;
    exit;
  end;
  if RegQueryStringValue(HKCU, SubKey, ValueName, OutValue) then
    Result := True;
end;

function GetInstalledVersionFromRegistry: String;
var
  Ver: String;
begin
  Result := '';
  if QueryUninstallString(InnoUninstallRegKey, 'DisplayVersion', Ver) then
    Result := Ver
  else if QueryUninstallString(LegacyUninstallRegKey, 'DisplayVersion', Ver) then
    Result := Ver;
end;

function GetInstalledVersionFromMeta: String;
var
  MetaPath: String;
begin
  Result := '';
  MetaPath := PluginInstallRoot + '\install-meta.ini';
  if FileExists(MetaPath) then
    Result := GetIniString('Install', 'DisplayVersion', '', MetaPath);
end;

function DetectPreviousVersion: String;
var
  Ver: String;
begin
  Ver := GetInstalledVersionFromMeta;
  if Ver = '' then
    Ver := GetInstalledVersionFromRegistry;
  Result := Ver;
end;

function PluginDllExists: Boolean;
begin
  Result := FileExists(PluginInstallRoot + '\bin\64bit\obs-shorts-vertical.dll');
end;

function IsUpgradeInstall: Boolean;
begin
  Result := (DetectPreviousVersion <> '') or PluginDllExists;
end;

function CompareVersionParts(const A, B: String): Integer;
var
  AMaj, AMin, APat, BMaj, BMin, BPat: Int64;
  ARest, BRest: String;
begin
  ARest := A;
  BRest := B;
  AMaj := StrToIntDef(Copy(ARest, 1, Pos('.', ARest + '.') - 1), 0);
  Delete(ARest, 1, Pos('.', ARest + '.'));
  AMin := StrToIntDef(Copy(ARest, 1, Pos('.', ARest + '.') - 1), 0);
  Delete(ARest, 1, Pos('.', ARest + '.'));
  APat := StrToIntDef(Copy(ARest, 1, Pos('.', ARest + '.') - 1), 0);

  BMaj := StrToIntDef(Copy(BRest, 1, Pos('.', BRest + '.') - 1), 0);
  Delete(BRest, 1, Pos('.', BRest + '.'));
  BMin := StrToIntDef(Copy(BRest, 1, Pos('.', BRest + '.') - 1), 0);
  Delete(BRest, 1, Pos('.', BRest + '.'));
  BPat := StrToIntDef(Copy(BRest, 1, Pos('.', BRest + '.') - 1), 0);

  if AMaj <> BMaj then begin Result := AMaj - BMaj; exit; end;
  if AMin <> BMin then begin Result := AMin - BMin; exit; end;
  Result := APat - BPat;
end;

function IsOBSRunning: Boolean;
begin
  Result := (FindWindowByClassName(OBS_WINDOW_CLASS) <> 0) or
            CheckForMutexes('OBSStudioRunningMutex') or
            CheckForMutexes('OBS32RunningMutex');
end;

function TryCloseOBSWindows: Boolean;
var
  Wnd: HWND;
  I: Integer;
begin
  Result := True;
  for I := 1 to 60 do begin
    Wnd := FindWindowByClassName(OBS_WINDOW_CLASS);
    if Wnd = 0 then begin
      { Window gone — wait briefly for process/mutex teardown }
      Sleep(500);
      Result := not IsOBSRunning;
      exit;
    end;
    PostMessage(Wnd, WM_CLOSE, 0, 0);
    Sleep(250);
  end;
  Result := not IsOBSRunning;
end;

function ConfirmUpgrade(const PrevVer, NewVer: String): Boolean;
var
  Body: String;
begin
  Body :=
    'Vertical Shorts Plugin ' + PrevVer + ' is currently installed.'#13#10#13#10 +
    'Setup will upgrade it to Vertical Shorts Plugin ' + NewVer + '.'#13#10#13#10 +
    'Your scenes, sources, destinations, credentials, schedules, and settings will be preserved.'#13#10 +
    'You do not need to uninstall first.';
  Result := TaskDialogMsgBox('Upgrade Vertical Shorts Plugin', Body, mbInformation,
    MB_OKCANCEL, ['&Upgrade', 'Cancel'], 0) = IDOK;
end;

function EnsureOBSClosed: Boolean;
var
  Answer: Integer;
begin
  Result := True;
  if not IsOBSRunning then
    exit;

  Answer := TaskDialogMsgBox(
    'OBS Studio must be closed',
    'OBS Studio must be closed before Vertical Shorts Plugin can be updated.'#13#10#13#10 +
    'Setup will ask OBS to quit normally. The plugin DLL cannot be replaced while OBS has it loaded.'#13#10#13#10 +
    'OBS will not be force-killed without your confirmation.',
    mbConfirmation, MB_OKCANCEL, ['&Close OBS and Continue', 'Cancel'], 0);

  if Answer <> IDOK then begin
    Result := False;
    exit;
  end;

  if not TryCloseOBSWindows then begin
    MsgBox(
      'OBS Studio is still running.'#13#10#13#10 +
      'Please close OBS manually, then run Setup again.'#13#10 +
      'The plugin DLL cannot be replaced while OBS has it loaded.',
      mbError, MB_OK);
    Result := False;
  end;
end;

function CopyFileIfExists(const Src, Dest: String): Boolean;
begin
  Result := False;
  if FileExists(Src) then
    Result := FileCopy(Src, Dest, False);
end;

function CreateUpgradeBackup: Boolean;
var
  Stamp, Dest, Root, PluginCfg: String;
begin
  Result := True;
  Stamp := GetDateTimeString('yyyymmdd_hhnnss', #0, #0);
  Dest := ExpandConstant('{localappdata}\VerticalShortsPlugin\upgrade-backups\' + Stamp);
  GUpgradeBackupDir := Dest;
  if not ForceDirectories(Dest) then begin
    Result := False;
    exit;
  end;

  Root := PluginInstallRoot;
  CopyFileIfExists(Root + '\install-meta.ini', Dest + '\install-meta.ini');

  { Lightweight plugin_config copy only — never duplicate recordings/media. }
  PluginCfg := ExpandConstant('{userappdata}\obs-studio\plugin_config\obs-shorts-vertical');
  if DirExists(PluginCfg) then begin
    ForceDirectories(Dest + '\plugin_config');
    CopyFileIfExists(PluginCfg + '\config.json', Dest + '\plugin_config\config.json');
    CopyFileIfExists(PluginCfg + '\settings.json', Dest + '\plugin_config\settings.json');
  end;

  SaveStringToFile(Dest + '\upgrade-info.txt',
    'Product={#MyAppName}'#13#10 +
    'PreviousVersion=' + GPreviousVersion + #13#10 +
    'NewVersion={#MyAppVersion}'#13#10 +
    'AppId={' + '{#MyAppIdGuid}' + '}'#13#10 +
    'AppDir=' + Root + #13#10 +
    'PreviousDllExists=' + IntToStr(Integer(PluginDllExists)) + #13#10 +
    'UserConfig=OBS scene collection key obs-shorts-vertical (not overwritten)'#13#10 +
    'Credentials=Windows Credential Manager (not overwritten)'#13#10 +
    'Note=Backup excludes recordings and large media files.'#13#10,
    False);
end;

function RemoveObsoletePluginBins: Boolean;
var
  Root, P: String;
begin
  Result := True;
  Root := PluginInstallRoot;
  { Only Vertical Shorts leftovers under the plugin tree — never OBS core or user data. }
  P := Root + '\bin\64bit\obs-shorts-vertical.pdb';
  if FileExists(P) then
    DeleteFile(P);
  P := Root + '\data\icons';
  if DirExists(P) then
    DelTree(P, True, True, True);
end;

function InitializeSetup: Boolean;
var
  Answer: Integer;
  Prev, Cur: String;
begin
  Result := True;
  Cur := '{#MyAppVersion}';
  Prev := DetectPreviousVersion;
  GPreviousVersion := Prev;
  GIsUpgrade := IsUpgradeInstall;
  GUpgradeBackupDir := '';

  if GIsUpgrade then begin
    if Prev = '' then
      Prev := '(unknown)';

    if not ConfirmUpgrade(Prev, Cur) then begin
      Result := False;
      exit;
    end;

    if (GPreviousVersion <> '') and (CompareVersionParts(GPreviousVersion, Cur) > 0) then begin
      Answer := MsgBox(
        'A newer version (' + GPreviousVersion + ') appears to be installed than this package (' + Cur + ').'#13#10#13#10 +
        'Installing an older package is not recommended and will not downgrade your configuration schema.'#13#10#13#10 +
        'Continue anyway?',
        mbConfirmation, MB_YESNO);
      if Answer <> IDYES then begin
        Result := False;
        exit;
      end;
    end;
  end;

  if not EnsureOBSClosed then
    Result := False;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  NeedsRestart := False;
  Result := '';

  if IsOBSRunning then begin
    if not EnsureOBSClosed then begin
      Result := 'OBS Studio is still running. Close it and retry Setup.';
      exit;
    end;
  end;

  if GIsUpgrade then begin
    if not CreateUpgradeBackup then begin
      Result := 'Could not create a lightweight upgrade backup under LocalAppData.';
      exit;
    end;
    RemoveObsoletePluginBins;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  MetaPath: String;
begin
  if CurStep = ssPostInstall then begin
    { Fresh install-meta for version detection on the next upgrade. }
    MetaPath := ExpandConstant('{app}\install-meta.ini');
    SetIniString('Install', 'DisplayName', '{#MyAppName}', MetaPath);
    SetIniString('Install', 'DisplayVersion', '{#MyAppVersion}', MetaPath);
    SetIniString('Install', 'AppId', '{' + '{#MyAppIdGuid}' + '}', MetaPath);
    SetIniString('Install', 'InstallDir', ExpandConstant('{app}'), MetaPath);
    SetIniString('Install', 'InstalledTimestampUtc',
      GetDateTimeString('yyyy-mm-dd"T"hh:nn:ss"Z"', #0, #0), MetaPath);
    SetIniString('Install', 'UpgradeBackup', GUpgradeBackupDir, MetaPath);
    SetIniString('Install', 'ConfigLocation',
      'OBS scene collection key obs-shorts-vertical + Windows Credential Manager', MetaPath);
    SetIniString('Install', 'Notes',
      'Binaries only under InstallDir. User config is never stored in overwritten plugin files.', MetaPath);

    { Clean mistaken AppData plugin copies from older builds (binaries only). }
    if DirExists(ExpandConstant('{userappdata}\obs-studio\plugins\obs-shorts-vertical')) then
      DelTree(ExpandConstant('{userappdata}\obs-studio\plugins\obs-shorts-vertical'), True, True, True);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  { Uninstall removes binaries only. Scene-collection settings and Credential
    Manager secrets are left intact so a reinstall can restore the workspace. }
  if CurUninstallStep = usPostUninstall then begin
    { intentionally no deletion of AppData scene collections or credentials }
  end;
end;
