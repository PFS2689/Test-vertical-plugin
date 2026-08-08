; Vertical Shorts Plugin — Inno Setup 6
;
; PERMANENT AppId (never change across versions):
;   {D4336EAC-D873-4E6B-8575-07096987E0C8}
; Same GUID as buildspec.json → uuids.windowsApp
;
; Install root is the OBS Studio directory ({app} = OBS root), for example:
;   C:\Program Files\obs-studio
;
; Plugin destinations (valid only after {app} is initialized):
;   {app}\obs-plugins\64bit\obs-shorts-vertical.dll
;   {app}\data\obs-plugins\obs-shorts-vertical\
;
; IMPORTANT: Never ExpandConstant('{app}') inside InitializeSetup (or any
; pre-directory-init path). That raises:
;   Internal error: An attempt was made to expand the "{app}" constant
;   before it was initialized.
;
; User configuration is NOT stored under {app}. It lives in the OBS scene
; collection ("obs-shorts-vertical") + Windows Credential Manager.

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
; Default OBS root — resolved via GetDefaultDirName (never ExpandConstant('{app}')).
DefaultDirName={code:GetDefaultDirName}
; Do not reuse a previous ProgramData plugin folder as {app} (OBS root is required).
UsePreviousAppDir=no
DisableProgramGroupPage=yes
; Allow browse when OBS is not at the default location.
DisableDirPage=no
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
AlwaysRestart=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; DLL → OBS obs-plugins\64bit (only during install phase; {app} is valid here)
Source: "{#SourceDir}\obs-shorts-vertical\bin\64bit\obs-shorts-vertical.dll"; \
    DestDir: "{app}\obs-plugins\64bit"; \
    Flags: ignoreversion uninsrestartdelete
; Resources → OBS data\obs-plugins\obs-shorts-vertical\
Source: "{#SourceDir}\obs-shorts-vertical\data\*"; \
    DestDir: "{app}\data\obs-plugins\obs-shorts-vertical"; \
    Flags: ignoreversion recursesubdirs createallsubdirs uninsrestartdelete
Source: "{#SourceDir}\obs-shorts-vertical\install-meta.ini"; \
    DestDir: "{app}\data\obs-plugins\obs-shorts-vertical"; \
    Flags: ignoreversion
Source: "{#SourceDir}\obs-shorts-vertical\INSTALL.txt"; \
    DestDir: "{app}\data\obs-plugins\obs-shorts-vertical"; \
    Flags: ignoreversion skipifsourcedoesntexist

[UninstallDelete]
Type: files; Name: "{app}\obs-plugins\64bit\obs-shorts-vertical.dll"
Type: files; Name: "{app}\obs-plugins\64bit\obs-shorts-vertical.pdb"
Type: filesandordirs; Name: "{app}\data\obs-plugins\obs-shorts-vertical"

[Code]
const
  OBS_WINDOW_CLASS = 'OBSWindowClass';
  WM_CLOSE = $0010;

var
  GIsUpgrade: Boolean;
  GPreviousVersion: String;
  GUpgradeBackupDir: String;
  GObsInstallPath: String; { Detected OBS root; never requires {app} }
  GExistingPluginDll: String;

function AddBackslashIfNeeded(const Path: String): String;
begin
  Result := Path;
  if (Result <> '') and (Result[Length(Result)] <> '\') then
    Result := Result + '\';
end;

function IsValidObsDir(const Dir: String): Boolean;
begin
  Result := (Dir <> '') and FileExists(AddBackslashIfNeeded(Dir) + 'bin\64bit\obs64.exe');
end;

function InnoUninstallRegKey: String;
begin
  Result := 'Software\Microsoft\Windows\CurrentVersion\Uninstall\{' +
            '{#MyAppIdGuid}' + '}_is1';
end;

function LegacyUninstallRegKey: String;
begin
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

{ --- OBS detection (safe before {app} is initialized) --- }

function DetectObsInstallPath: String;
var
  Candidate, RegVal: String;
begin
  Result := '';

  { Uninstall location from a previous Vertical Shorts install (may be OBS root). }
  if QueryUninstallString(InnoUninstallRegKey, 'InstallLocation', RegVal) and IsValidObsDir(RegVal) then begin
    Result := RemoveBackslash(RegVal);
    exit;
  end;
  if QueryUninstallString(LegacyUninstallRegKey, 'InstallLocation', RegVal) and IsValidObsDir(RegVal) then begin
    Result := RemoveBackslash(RegVal);
    exit;
  end;

  { OBS Studio uninstall / install keys (best-effort). }
  if RegQueryStringValue(HKLM64, 'Software\OBS Studio', '', RegVal) and IsValidObsDir(RegVal) then begin
    Result := RemoveBackslash(RegVal);
    exit;
  end;
  if QueryUninstallString('Software\Microsoft\Windows\CurrentVersion\Uninstall\OBS Studio', 'InstallLocation', RegVal) and IsValidObsDir(RegVal) then begin
    Result := RemoveBackslash(RegVal);
    exit;
  end;

  { Known default: C:\Program Files\obs-studio }
  Candidate := ExpandConstant('{autopf}\obs-studio');
  if IsValidObsDir(Candidate) then begin
    Result := Candidate;
    exit;
  end;

  { 32-bit Program Files fallback on some systems }
  Candidate := ExpandConstant('{pf}\obs-studio');
  if IsValidObsDir(Candidate) then begin
    Result := Candidate;
    exit;
  end;
end;

{ Called by DefaultDirName={code:GetDefaultDirName} — must not use {app}. }
function GetDefaultDirName(Param: String): String;
begin
  Result := DetectObsInstallPath;
  if Result = '' then
    Result := ExpandConstant('{autopf}\obs-studio');
end;

{ --- Upgrade detection without {app} --- }

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

function GetInstalledVersionFromMetaFile(const MetaPath: String): String;
begin
  Result := '';
  if FileExists(MetaPath) then
    Result := GetIniString('Install', 'DisplayVersion', '', MetaPath);
end;

function FindExistingPluginDll: String;
var
  ObsRoot, Candidate, RegVal: String;
begin
  Result := '';

  { New layout under detected / default OBS roots }
  ObsRoot := GObsInstallPath;
  if ObsRoot = '' then
    ObsRoot := ExpandConstant('{autopf}\obs-studio');
  Candidate := AddBackslashIfNeeded(ObsRoot) + 'obs-plugins\64bit\obs-shorts-vertical.dll';
  if FileExists(Candidate) then begin
    Result := Candidate;
    exit;
  end;

  Candidate := ExpandConstant('{autopf}\obs-studio\obs-plugins\64bit\obs-shorts-vertical.dll');
  if FileExists(Candidate) then begin
    Result := Candidate;
    exit;
  end;

  { InstallLocation from uninstall registry may point at OBS root or an old plugin tree. }
  if QueryUninstallString(InnoUninstallRegKey, 'InstallLocation', RegVal) then begin
    Candidate := AddBackslashIfNeeded(RegVal) + 'obs-plugins\64bit\obs-shorts-vertical.dll';
    if FileExists(Candidate) then begin
      Result := Candidate;
      exit;
    end;
    Candidate := AddBackslashIfNeeded(RegVal) + 'bin\64bit\obs-shorts-vertical.dll';
    if FileExists(Candidate) then begin
      Result := Candidate;
      exit;
    end;
  end;

  { Legacy ProgramData third-party path from earlier 1.0.5 builds }
  Candidate := ExpandConstant('{commonappdata}\obs-studio\plugins\obs-shorts-vertical\bin\64bit\obs-shorts-vertical.dll');
  if FileExists(Candidate) then
    Result := Candidate;
end;

function DetectPreviousVersion: String;
var
  Ver, DllPath, MetaPath, Root: String;
begin
  Ver := GetInstalledVersionFromRegistry;

  DllPath := FindExistingPluginDll;
  if (Ver = '') and (DllPath <> '') then begin
    if Pos('\obs-plugins\', LowerCase(DllPath)) > 0 then begin
      { ...\obs-studio\obs-plugins\64bit\dll → OBS root }
      Root := ExtractFileDir(ExtractFileDir(ExtractFileDir(DllPath)));
      MetaPath := AddBackslashIfNeeded(Root) + 'data\obs-plugins\obs-shorts-vertical\install-meta.ini';
    end else begin
      { Legacy ...\obs-shorts-vertical\bin\64bit\dll → plugin root }
      Root := ExtractFileDir(ExtractFileDir(ExtractFileDir(DllPath)));
      MetaPath := AddBackslashIfNeeded(Root) + 'install-meta.ini';
    end;
    Ver := GetInstalledVersionFromMetaFile(MetaPath);
  end;

  Result := Ver;
end;

function IsUpgradeInstall: Boolean;
begin
  Result := (DetectPreviousVersion <> '') or (FindExistingPluginDll <> '');
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
  { Window/mutex check only — no {app} paths. }
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
  Stamp, Dest, MetaPath, PluginCfg: String;
begin
  Result := True;
  Stamp := GetDateTimeString('yyyymmdd_hhnnss', #0, #0);
  Dest := ExpandConstant('{localappdata}\VerticalShortsPlugin\upgrade-backups\' + Stamp);
  GUpgradeBackupDir := Dest;
  if not ForceDirectories(Dest) then begin
    Result := False;
    exit;
  end;

  { {app} is initialized by PrepareToInstall time. }
  MetaPath := ExpandConstant('{app}\data\obs-plugins\obs-shorts-vertical\install-meta.ini');
  CopyFileIfExists(MetaPath, Dest + '\install-meta.ini');
  if (GExistingPluginDll <> '') and FileExists(GExistingPluginDll) then
    SaveStringToFile(Dest + '\previous-dll-path.txt', GExistingPluginDll + #13#10, False);

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
    'ObsInstallPath=' + GObsInstallPath + #13#10 +
    'AppDir=' + ExpandConstant('{app}') + #13#10 +
    'PreviousDll=' + GExistingPluginDll + #13#10 +
    'UserConfig=OBS scene collection key obs-shorts-vertical (not overwritten)'#13#10 +
    'Credentials=Windows Credential Manager (not overwritten)'#13#10 +
    'Note=Backup excludes recordings and large media files.'#13#10,
    False);
end;

function RemoveObsoletePluginBins: Boolean;
var
  P: String;
begin
  Result := True;
  { New layout leftovers under OBS root ({app} valid in PrepareToInstall). }
  P := ExpandConstant('{app}\obs-plugins\64bit\obs-shorts-vertical.pdb');
  if FileExists(P) then
    DeleteFile(P);

  { Legacy ProgramData tree from earlier builds — binaries only. }
  P := ExpandConstant('{commonappdata}\obs-studio\plugins\obs-shorts-vertical');
  if DirExists(P) then
    DelTree(P, True, True, True);

  P := ExpandConstant('{userappdata}\obs-studio\plugins\obs-shorts-vertical');
  if DirExists(P) then
    DelTree(P, True, True, True);
end;

function InitializeSetup: Boolean;
var
  Answer: Integer;
  Prev, Cur: String;
begin
  Result := True;

  { Detect OBS without touching {app}. }
  GObsInstallPath := DetectObsInstallPath;
  GExistingPluginDll := FindExistingPluginDll;
  GUpgradeBackupDir := '';

  Cur := '{#MyAppVersion}';
  Prev := DetectPreviousVersion;
  GPreviousVersion := Prev;
  GIsUpgrade := IsUpgradeInstall;

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

procedure InitializeWizard;
begin
  { Always prefer a validated OBS root — never a legacy ProgramData plugin tree. }
  if GObsInstallPath <> '' then
    WizardForm.DirEdit.Text := GObsInstallPath
  else if not IsValidObsDir(WizardDirValue) then
    WizardForm.DirEdit.Text := ExpandConstant('{autopf}\obs-studio');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpSelectDir then begin
    if not IsValidObsDir(WizardDirValue) then begin
      MsgBox(
        'Please select a valid OBS Studio installation folder.'#13#10#13#10 +
        'It must contain:'#13#10 +
        '  bin\64bit\obs64.exe'#13#10#13#10 +
        'Example:'#13#10 +
        '  C:\Program Files\obs-studio',
        mbError, MB_OK);
      Result := False;
    end else
      GObsInstallPath := WizardDirValue;
  end;
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result :=
    'OBS Studio folder:' + NewLine +
    Space + WizardDirValue + NewLine + NewLine +
    'Plugin DLL:' + NewLine +
    Space + WizardDirValue + '\obs-plugins\64bit\obs-shorts-vertical.dll' + NewLine + NewLine +
    'Plugin data:' + NewLine +
    Space + WizardDirValue + '\data\obs-plugins\obs-shorts-vertical\' + NewLine;
  if GIsUpgrade then
    Result := Result + NewLine + 'Mode: Upgrade (settings preserved)' + NewLine
  else
    Result := Result + NewLine + 'Mode: Fresh install' + NewLine;
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

  if not IsValidObsDir(ExpandConstant('{app}')) then begin
    Result := 'Selected folder is not a valid OBS Studio installation (missing bin\64bit\obs64.exe).';
    exit;
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
    MetaPath := ExpandConstant('{app}\data\obs-plugins\obs-shorts-vertical\install-meta.ini');
    SetIniString('Install', 'DisplayName', '{#MyAppName}', MetaPath);
    SetIniString('Install', 'DisplayVersion', '{#MyAppVersion}', MetaPath);
    SetIniString('Install', 'AppId', '{' + '{#MyAppIdGuid}' + '}', MetaPath);
    SetIniString('Install', 'InstallDir', ExpandConstant('{app}'), MetaPath);
    SetIniString('Install', 'PluginDll',
      ExpandConstant('{app}\obs-plugins\64bit\obs-shorts-vertical.dll'), MetaPath);
    SetIniString('Install', 'PluginData',
      ExpandConstant('{app}\data\obs-plugins\obs-shorts-vertical'), MetaPath);
    SetIniString('Install', 'InstalledTimestampUtc',
      GetDateTimeString('yyyy-mm-dd"T"hh:nn:ss"Z"', #0, #0), MetaPath);
    SetIniString('Install', 'UpgradeBackup', GUpgradeBackupDir, MetaPath);
    SetIniString('Install', 'ConfigLocation',
      'OBS scene collection key obs-shorts-vertical + Windows Credential Manager', MetaPath);
    SetIniString('Install', 'Notes',
      'DLL under obs-plugins\64bit; data under data\obs-plugins\obs-shorts-vertical. User config is never overwritten.', MetaPath);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then begin
    { Binaries removed by [Files]/[UninstallDelete]. Scene collections and
      Credential Manager secrets are intentionally left intact. }
  end;
end;
