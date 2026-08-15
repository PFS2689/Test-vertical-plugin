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
; Restart Manager: detect apps locking files we will replace (e.g. obs64.exe holding the plugin DLL).
; Setup will prompt — never force-kill OBS (that would interrupt streams/recordings).
CloseApplications=yes
CloseApplicationsFilter=obs*.exe,*.dll
RestartApplications=no
RestartIfNeededByRun=no
CreateUninstallRegKey=yes
UpdateUninstallLogAppName=yes
AllowCancelDuringInstall=yes
UsedUserAreasWarning=no
AlwaysRestart=no

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

; No [InstallDelete] — upgrades must never wipe user configuration.
; [UninstallDelete] only removes installer-owned binaries/data under the OBS tree.
; OBS scene collections + Credential Manager secrets are never deleted on upgrade/uninstall.

[Files]
; DLL → OBS obs-plugins\64bit (only during install phase; {app} is valid here)
; Same product version (1.0.5) across rebuilds: PE ProductVersion stays 1.0.5, but
; FileVersion 4th component changes per CI build. Use replacesameversion so a
; same-product-version development upgrade STILL replaces the DLL.
; Do NOT rely on ignoreversion alone for this path — replacesameversion is the
; correct same-version upgrade flag. Do NOT use restartreplace as the normal path.
Source: "{#SourceDir}\obs-shorts-vertical\bin\64bit\obs-shorts-vertical.dll"; \
    DestDir: "{app}\obs-plugins\64bit"; \
    Flags: replacesameversion uninsrestartdelete
; Resources → OBS data\obs-plugins\obs-shorts-vertical\
; Locale/data files often lack PE version resources — ignoreversion is appropriate here.
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
; Uninstall-only cleanup of plugin binaries/data. Never runs during an in-place upgrade.
Type: files; Name: "{app}\obs-plugins\64bit\obs-shorts-vertical.dll"
Type: files; Name: "{app}\obs-plugins\64bit\obs-shorts-vertical.pdb"
Type: filesandordirs; Name: "{app}\data\obs-plugins\obs-shorts-vertical"

[Code]
const
  OBS_WINDOW_CLASS = 'OBSWindowClass';
  TH32CS_SNAPPROCESS = $00000002;
  MAX_PATH_CHARS = 260;
  INVALID_HANDLE_VALUE = -1;
  VS_GENERIC_READ = $80000000;
  VS_GENERIC_WRITE = $40000000;
  VS_OPEN_EXISTING = 3;
  VS_CREATE_ALWAYS = 2;
  VS_FILE_SHARE_NONE = 0;
  VS_FILE_FLAG_DELETE_ON_CLOSE = $04000000;
  VS_ERROR_ACCESS_DENIED = 5;
  VS_ERROR_SHARING_VIOLATION = 32;
  VS_ERROR_LOCK_VIOLATION = 33;

  { PluginDllStatus values returned by ProbePluginDllStatus }
  PDS_OK = 0;
  PDS_MISSING = 1;
  PDS_LOCKED = 2;
  PDS_PERMISSION = 3;

type
  TProcessEntry32 = record
    dwSize: DWORD;
    cntUsage: DWORD;
    th32ProcessID: DWORD;
    th32DefaultHeapID: Int64;
    th32ModuleID: DWORD;
    cntThreads: DWORD;
    th32ParentProcessID: DWORD;
    pcPriClassBase: Longint;
    dwFlags: DWORD;
    szExeFile: array[0..MAX_PATH_CHARS - 1] of Char;
  end;

var
  GIsUpgrade: Boolean;
  GPreviousVersion: String;
  GUpgradeBackupDir: String;
  GObsInstallPath: String; (* Detected OBS root; never requires app constant *)
  GExistingPluginDll: String;

function CreateToolhelp32Snapshot(dwFlags, th32ProcessID: DWORD): THandle;
  external 'CreateToolhelp32Snapshot@kernel32.dll stdcall';
function Process32FirstW(hSnapshot: THandle; var lppe: TProcessEntry32): BOOL;
  external 'Process32FirstW@kernel32.dll stdcall';
function Process32NextW(hSnapshot: THandle; var lppe: TProcessEntry32): BOOL;
  external 'Process32NextW@kernel32.dll stdcall';
function CloseHandle(hObject: THandle): BOOL;
  external 'CloseHandle@kernel32.dll stdcall';
function CreateFileW(lpFileName: string; dwDesiredAccess, dwShareMode: DWORD;
  lpSecurityAttributes: DWORD; dwCreationDisposition, dwFlagsAndAttributes: DWORD;
  hTemplateFile: THandle): THandle;
  external 'CreateFileW@kernel32.dll stdcall';
function GetLastError: DWORD;
  external 'GetLastError@kernel32.dll stdcall';
procedure SetLastError(dwErrCode: DWORD);
  external 'SetLastError@kernel32.dll stdcall';

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

(* OBS detection — safe before app constant is initialized *)

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

(* Called by DefaultDirName=code:GetDefaultDirName — must not use app constant. *)
function GetDefaultDirName(Param: String): String;
begin
  Result := DetectObsInstallPath;
  if Result = '' then
    Result := ExpandConstant('{autopf}\obs-studio');
end;

(* Upgrade detection without app constant *)

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

function IsObsProcessName(const ExeName: String): Boolean;
var
  N: String;
begin
  N := LowerCase(ExtractFileName(ExeName));
  Result := (N = 'obs64.exe') or (N = 'obs32.exe') or (N = 'obs.exe');
end;

function ProcessEntryExeName(const Entry: TProcessEntry32): String;
var
  I: Integer;
begin
  Result := '';
  for I := 0 to MAX_PATH_CHARS - 1 do begin
    if Entry.szExeFile[I] = #0 then
      Break;
    Result := Result + Entry.szExeFile[I];
  end;
end;

function IsOBSProcessRunning: Boolean;
var
  Snapshot: THandle;
  Entry: TProcessEntry32;
begin
  Result := False;
  Snapshot := CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if Snapshot = INVALID_HANDLE_VALUE then
    exit;

  Entry.dwSize := SizeOf(Entry);
  if Process32FirstW(Snapshot, Entry) then begin
    repeat
      if IsObsProcessName(ProcessEntryExeName(Entry)) then begin
        Result := True;
        Break;
      end;
    until not Process32NextW(Snapshot, Entry);
  end;
  CloseHandle(Snapshot);
end;

function ResolvePluginDllPath: String;
var
  Candidate: String;
begin
  Result := '';

  { Prefer known existing DLL path from upgrade detection. }
  if (GExistingPluginDll <> '') and FileExists(GExistingPluginDll) then begin
    Result := GExistingPluginDll;
    exit;
  end;

  { Wizard / app dir once available. }
  try
    Candidate := ExpandConstant('{app}\obs-plugins\64bit\obs-shorts-vertical.dll');
    if FileExists(Candidate) then begin
      Result := Candidate;
      exit;
    end;
  except
  end;

  if GObsInstallPath <> '' then begin
    Candidate := AddBackslashIfNeeded(GObsInstallPath) +
      'obs-plugins\64bit\obs-shorts-vertical.dll';
    if FileExists(Candidate) then begin
      Result := Candidate;
      exit;
    end;
  end;

  Candidate := ExpandConstant('{autopf}\obs-studio\obs-plugins\64bit\obs-shorts-vertical.dll');
  if FileExists(Candidate) then
    Result := Candidate;
end;

function ResolvePluginDir: String;
var
  DllPath: String;
begin
  DllPath := ResolvePluginDllPath;
  if DllPath <> '' then begin
    Result := ExtractFileDir(DllPath);
    exit;
  end;

  try
    Result := ExpandConstant('{app}\obs-plugins\64bit');
    exit;
  except
  end;

  if GObsInstallPath <> '' then
    Result := AddBackslashIfNeeded(GObsInstallPath) + 'obs-plugins\64bit'
  else
    Result := ExpandConstant('{autopf}\obs-studio\obs-plugins\64bit');
end;

function IsOBSRunning: Boolean;
begin
  { Window + mutex + process scan. Process scan catches obs64.exe even if
    the main window is hidden or mutex names differ across OBS builds. }
  Result := (FindWindowByClassName(OBS_WINDOW_CLASS) <> 0) or
            CheckForMutexes('OBSStudioRunningMutex') or
            CheckForMutexes('OBS32RunningMutex') or
            IsOBSProcessRunning;
end;

(* Probe existing DLL / plugin dir.
   Distinguishes: replaceable (OK), missing, locked by another process, ACL/permission. *)
function ProbePluginDllStatus: Integer;
var
  DllPath, DirPath, ProbePath: String;
  H: THandle;
  Err: DWORD;
begin
  DllPath := ResolvePluginDllPath;
  if (DllPath = '') or (not FileExists(DllPath)) then begin
    Result := PDS_MISSING;
    exit;
  end;

  SetLastError(0);
  H := CreateFileW(DllPath, VS_GENERIC_READ or VS_GENERIC_WRITE, VS_FILE_SHARE_NONE, 0,
    VS_OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
  if H <> INVALID_HANDLE_VALUE then begin
    CloseHandle(H);
    Result := PDS_OK;
    exit;
  end;

  Err := GetLastError;
  if (Err = VS_ERROR_SHARING_VIOLATION) or (Err = VS_ERROR_LOCK_VIOLATION) then begin
    Result := PDS_LOCKED;
    exit;
  end;

  { ERROR_ACCESS_DENIED (5) is ambiguous on Windows — often means in-use.
    If OBS is running, treat as locked. Otherwise distinguish via a dir write probe. }
  if IsOBSRunning then begin
    Result := PDS_LOCKED;
    exit;
  end;

  DirPath := ResolvePluginDir;
  ProbePath := AddBackslashIfNeeded(DirPath) + '~vs-write-probe.tmp';
  SetLastError(0);
  H := CreateFileW(ProbePath, VS_GENERIC_WRITE, VS_FILE_SHARE_NONE, 0, VS_CREATE_ALWAYS,
    FILE_ATTRIBUTE_NORMAL or VS_FILE_FLAG_DELETE_ON_CLOSE, 0);
  if H = INVALID_HANDLE_VALUE then begin
    Err := GetLastError;
    if (Err = VS_ERROR_ACCESS_DENIED) or (not IsAdminInstallMode) then
      Result := PDS_PERMISSION
    else
      Result := PDS_LOCKED;
  end else begin
    CloseHandle(H);
    { Directory is writable but DLL open failed without OBS → still in use. }
    Result := PDS_LOCKED;
  end;
end;

function IsPluginDllLocked: Boolean;
begin
  Result := ProbePluginDllStatus = PDS_LOCKED;
end;

function ExistingPluginNeedsReplace: Boolean;
begin
  Result := ResolvePluginDllPath <> '';
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

function ShowObsRunningDialog: Integer;
begin
  Result := MsgBox(
    'OBS Studio is currently running.'#13#10#13#10 +
    'OBS must be closed before Vertical Shorts can be installed or updated.'#13#10#13#10 +
    'Please close OBS Studio, then click Retry.',
    mbConfirmation, MB_RETRYCANCEL);
end;

function ShowDllStillLockedDialog: Integer;
begin
  Result := MsgBox(
    'Vertical Shorts could not update because the existing plugin file is still in use.'#13#10#13#10 +
    'Please make sure OBS Studio is completely closed and try again.',
    mbError, MB_RETRYCANCEL);
end;

function ShowPermissionDeniedDialog: Integer;
begin
  Result := MsgBox(
    'Vertical Shorts could not update the OBS plugin directory.'#13#10#13#10 +
    'Please run the installer with administrator privileges.',
    mbError, MB_OK);
end;

(* Blocks until OBS is closed and the plugin DLL is unlocked — never force-kills OBS.
   Returns False if the user cancels or a non-retryable error occurs. *)
function EnsurePluginReplaceable: Boolean;
var
  Answer: Integer;
  Status: Integer;
  I: Integer;
begin
  Result := True;

  { Fresh install — nothing to replace. }
  if not ExistingPluginNeedsReplace then
    exit;

  while True do begin
    if IsOBSRunning then begin
      Answer := ShowObsRunningDialog;
      if Answer <> IDRETRY then begin
        Result := False;
        exit;
      end;
      for I := 1 to 40 do begin
        if not IsOBSRunning then
          Break;
        Sleep(250);
      end;
      Continue;
    end;

    Status := ProbePluginDllStatus;
    if (Status = PDS_OK) or (Status = PDS_MISSING) then begin
      Result := True;
      exit;
    end;

    if Status = PDS_PERMISSION then begin
      ShowPermissionDeniedDialog;
      Result := False;
      exit;
    end;

    { PDS_LOCKED — OBS may have exited but the module handle has not released yet. }
    Answer := ShowDllStillLockedDialog;
    if Answer <> IDRETRY then begin
      Result := False;
      exit;
    end;
    for I := 1 to 40 do begin
      Status := ProbePluginDllStatus;
      if (Status = PDS_OK) or (Status = PDS_MISSING) then
        Break;
      Sleep(250);
    end;
  end;
end;

(* Compatibility wrapper used by InitializeSetup / PrepareToInstall. *)
function EnsureOBSClosed: Boolean;
begin
  Result := EnsurePluginReplaceable;
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

  (* app constant is initialized by PrepareToInstall time. *)
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

  { Wrong / leftover DLL locations from older installers — remove duplicates only. }
  P := ExpandConstant('{app}\bin\64bit\obs-shorts-vertical.dll');
  if FileExists(P) then begin
    if not DeleteFile(P) then
      Log('WARNING: could not delete obsolete DLL: ' + P);
  end;
  P := ExpandConstant('{app}\bin\64bit\obs-shorts-vertical.pdb');
  if FileExists(P) then
    DeleteFile(P);

  P := ExpandConstant('{app}\obs-plugins\64bit\obs-shorts-vertical.pdb');
  if FileExists(P) then
    DeleteFile(P);

  { Stale rename leftovers — never leave side-by-side DLL copies. }
  P := ExpandConstant('{app}\obs-plugins\64bit\obs-shorts-vertical-old.dll');
  if FileExists(P) then
    DeleteFile(P);
  P := ExpandConstant('{app}\obs-plugins\64bit\obs-shorts-vertical-copy.dll');
  if FileExists(P) then
    DeleteFile(P);
  P := ExpandConstant('{app}\obs-plugins\64bit\obs-shorts-vertical-2.dll');
  if FileExists(P) then
    DeleteFile(P);

  { Legacy ProgramData / AppData third-party plugin trees — binaries only. }
  P := ExpandConstant('{commonappdata}\obs-studio\plugins\obs-shorts-vertical');
  if DirExists(P) then
    DelTree(P, True, True, True);

  P := ExpandConstant('{userappdata}\obs-studio\plugins\obs-shorts-vertical');
  if DirExists(P) then
    DelTree(P, True, True, True);

  P := ExpandConstant('{localappdata}\obs-studio\plugins\obs-shorts-vertical');
  if DirExists(P) then
    DelTree(P, True, True, True);
end;

function InitializeSetup: Boolean;
var
  Answer: Integer;
  Prev, Cur: String;
begin
  Result := True;

  (* Detect OBS without touching app constant. *)
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
    end else begin
      GObsInstallPath := WizardDirValue;
      GExistingPluginDll :=
        AddBackslashIfNeeded(WizardDirValue) + 'obs-plugins\64bit\obs-shorts-vertical.dll';
      if not FileExists(GExistingPluginDll) then
        GExistingPluginDll := FindExistingPluginDll;
      GIsUpgrade := IsUpgradeInstall;
    end;
  end else if CurPageID = wpReady then begin
    { Final gate before install phase — Retry loop, never force-kill OBS. }
    if not EnsurePluginReplaceable then
      Result := False;
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
var
  Status: Integer;
  AppDll: String;
begin
  NeedsRestart := False;
  Result := '';

  { Gate BEFORE file copy / DeleteFile — never reach "DeleteFile failed; code 5".
    Windows reboot must not be the normal upgrade path for this OBS plugin. }
  AppDll := ExpandConstant('{app}\obs-plugins\64bit\obs-shorts-vertical.dll');
  if FileExists(AppDll) then
    GExistingPluginDll := AppDll;

  if ExistingPluginNeedsReplace then begin
    if not EnsurePluginReplaceable then begin
      if IsOBSRunning then
        Result := 'OBS Studio must be closed before Vertical Shorts can be updated.'
      else begin
        Status := ProbePluginDllStatus;
        if Status = PDS_PERMISSION then
          Result :=
            'Vertical Shorts could not update the OBS plugin directory. ' +
            'Please run the installer with administrator privileges.'
        else
          Result :=
            'Vertical Shorts could not update because the existing plugin file is still in use. ' +
            'Please make sure OBS Studio is completely closed and try again.';
      end;
      exit;
    end;

    Status := ProbePluginDllStatus;
    if Status = PDS_LOCKED then begin
      Result :=
        'Vertical Shorts could not update because the existing plugin file is still in use. ' +
        'Please make sure OBS Studio is completely closed and try again.';
      exit;
    end;
    if Status = PDS_PERMISSION then begin
      Result :=
        'Vertical Shorts could not update the OBS plugin directory. ' +
        'Please run the installer with administrator privileges.';
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
    { Binary-only cleanup. Never touches scene collections or Credential Manager. }
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
    { Prefer Build ID stamped into staging install-meta.ini by the packaging script. }
    if GetIniString('Install', 'BuildId', '', MetaPath) = '' then
      SetIniString('Install', 'BuildId', '(see Settings → About / OBS log)', MetaPath);
    SetIniString('Install', 'ConfigLocation',
      'OBS scene collection key obs-shorts-vertical + Windows Credential Manager', MetaPath);
    SetIniString('Install', 'Notes',
      'DLL under obs-plugins\64bit; data under data\obs-plugins\obs-shorts-vertical. User config is never overwritten. Same-version upgrades use replacesameversion.', MetaPath);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then begin
    { Binaries removed by [Files]/[UninstallDelete]. Scene collections and
      Credential Manager secrets are intentionally left intact. }
  end;
end;
