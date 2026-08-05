; Vertical Shorts Plugin — Inno Setup 6 script
; Packages a pre-built staged OBS plugin payload only.
; Do not point SourceDir at the source tree; use release/staging from Package-Windows.ps1.
;
; Compile example:
;   ISCC.exe /DMyAppVersion=1.0.5 ^
;            /DSourceDir=C:\path\release\staging ^
;            /DOutputDir=C:\path\release ^
;            /DOutputBaseFilename="Vertical Shorts Plugin 1.0.5 Setup" ^
;            VerticalShortsPlugin.iss

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

[Setup]
; AppId GUID from buildspec.json uuids.windowsApp ({{ escapes to a single {)
AppId={{D4336EAC-D873-4E6B-8575-07096987E0C8}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={commonappdata}\obs-studio\plugins\obs-shorts-vertical
DisableProgramGroupPage=yes
DisableDirPage=yes
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

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Files]
; Stage layout must be: SourceDir\obs-shorts-vertical\bin\64bit\obs-shorts-vertical.dll
Source: "{#SourceDir}\obs-shorts-vertical\*"; DestDir: "{app}"; \
    Flags: ignoreversion recursesubdirs createallsubdirs

[Code]
function LegacyAppDataRoot: String;
begin
  Result := ExpandConstant('{userappdata}\obs-studio\plugins\obs-shorts-vertical');
end;

procedure RemoveLegacyAppDataInstall;
var
  Root: String;
begin
  { Older builds incorrectly installed under %APPDATA%; OBS does not load from there. }
  Root := LegacyAppDataRoot;
  if DirExists(Root) then
    DelTree(Root, True, True, True);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    RemoveLegacyAppDataInstall;
end;
