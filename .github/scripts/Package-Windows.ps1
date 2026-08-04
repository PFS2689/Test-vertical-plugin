[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( $env:CI -eq $null ) {
    throw "Package-Windows.ps1 requires CI environment"
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "Packaging script requires a 64-bit system to build and run."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The packaging script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

function Get-VcVarsBat {
    $vswhere = "${Env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if ( ! ( Test-Path $vswhere ) ) {
        throw "vswhere.exe not found; Visual Studio Build Tools are required."
    }
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ( ! $vsPath ) {
        throw "Visual Studio installation with MSVC tools not found."
    }
    $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
    if ( ! ( Test-Path $vcvars ) ) {
        throw "vcvars64.bat not found at $vcvars"
    }
    return $vcvars
}

function Build-CleanSetupExe {
    param(
        [string] $ProjectRoot,
        [string] $ReleaseDir,
        [string] $ProductVersion,
        [string] $SetupName
    )

    $PluginId = 'obs-shorts-vertical'
    $DllPath = Join-Path $ReleaseDir "$PluginId\bin\64bit\obs-shorts-vertical.dll"
    $LocalePath = Join-Path $ReleaseDir "$PluginId\data\locale\en-US.ini"
    $InstallTxt = Join-Path $ReleaseDir 'INSTALL.txt'
    $ManifestPath = Join-Path $ProjectRoot 'src\windows-setup\setup.manifest'
    $SetupSrc = Join-Path $ProjectRoot 'src\windows-setup\setup.c'
    $RcTemplate = Join-Path $ProjectRoot 'src\windows-setup\setup.rc.in'
    $HeaderPath = Join-Path $ProjectRoot 'src\windows-setup\setup_resources.h'

    foreach ($p in @($DllPath, $LocalePath, $InstallTxt, $ManifestPath, $SetupSrc, $RcTemplate, $HeaderPath)) {
        if ( ! ( Test-Path $p ) ) {
            throw "Required setup payload/source missing: $p"
        }
    }

    $parts = $ProductVersion.Split('.')
    if ( $parts.Count -lt 3 ) {
        throw "Plugin version must be MAJOR.MINOR.PATCH (got '$ProductVersion')"
    }
    $verMajor = [int]$parts[0]
    $verMinor = [int]$parts[1]
    $verPatch = [int]$parts[2]

    $workDir = Join-Path $ProjectRoot "release\setup-build"
    if ( Test-Path $workDir ) {
        Remove-Item -Recurse -Force $workDir
    }
    New-Item -ItemType Directory -Path $workDir | Out-Null

    # Resource compiler needs doubled backslashes in quoted path strings.
    function Escape-RcPath([string] $Path) {
        return $Path.Replace('\', '\\')
    }

    $rc = Get-Content -Raw -Path $RcTemplate
    $rc = $rc.Replace('PAYLOAD_DLL_PATH', (Escape-RcPath $DllPath))
    $rc = $rc.Replace('PAYLOAD_LOCALE_PATH', (Escape-RcPath $LocalePath))
    $rc = $rc.Replace('PAYLOAD_INSTALL_TXT_PATH', (Escape-RcPath $InstallTxt))
    $rc = $rc.Replace('PAYLOAD_MANIFEST_PATH', (Escape-RcPath $ManifestPath))
    $rc = $rc.Replace('PAYLOAD_VER_MAJOR', "$verMajor")
    $rc = $rc.Replace('PAYLOAD_VER_MINOR', "$verMinor")
    $rc = $rc.Replace('PAYLOAD_VER_PATCH', "$verPatch")
    $rc = $rc.Replace('PAYLOAD_VER_STRING', "$ProductVersion")
    $rcPath = Join-Path $workDir 'setup.rc'
    Set-Content -Path $rcPath -Value $rc -Encoding ascii

    # Copy header next to generated rc so rc.exe can include it easily.
    Copy-Item -Force $HeaderPath (Join-Path $workDir 'setup_resources.h')

    # Avoid /D string-quoting pitfalls on cmd.exe — emit a tiny header instead.
    $versionHeader = @"
#pragma once
#define VSP_SETUP_VERSION_A "$ProductVersion"
"@
    Set-Content -Path (Join-Path $workDir 'setup_version.h') -Value $versionHeader -Encoding ascii

    $outExe = Join-Path $ProjectRoot "release\${SetupName}.exe"
    if ( Test-Path $outExe ) {
        Remove-Item -Force $outExe
    }

    $includeDir = Join-Path $ProjectRoot 'src\windows-setup'
    $vcvars = Get-VcVarsBat
    $resPath = Join-Path $workDir 'setup.res'

    $batch = @"
@echo off
setlocal
call "$vcvars" || exit /b 1
cd /d "$workDir" || exit /b 1
rc.exe /nologo /i"$includeDir" /i"$workDir" /fo"$resPath" "$rcPath" || exit /b 1
cl.exe /nologo /O2 /W3 /DUNICODE /D_UNICODE /I"$includeDir" /I"$workDir" /Fe:"$outExe" "$SetupSrc" /link /SUBSYSTEM:WINDOWS /MACHINE:X64 /DYNAMICBASE /NXCOMPAT /PDBALTPATH:%%_PDB%% /INCREMENTAL:NO "$resPath" user32.lib shell32.lib
exit /b %ERRORLEVEL%
"@
    $batPath = Join-Path $workDir 'build-setup.bat'
    Set-Content -Path $batPath -Value $batch -Encoding ascii

    Log-Group "Building clean MSVC Setup.exe (no Inno Setup)..."
    $proc = Start-Process -FilePath 'cmd.exe' -ArgumentList @('/c', "`"$batPath`"") -Wait -PassThru -NoNewWindow
    if ( $proc.ExitCode -ne 0 ) {
        throw "Setup.exe compile failed with exit code $($proc.ExitCode)"
    }
    if ( ! ( Test-Path $outExe ) ) {
        throw "Setup.exe was not created at $outExe"
    }

    # Drop compiler junk from release/
    Get-ChildItem -Path $workDir -ErrorAction SilentlyContinue | Out-Null
    Remove-Item -Recurse -Force $workDir -ErrorAction SilentlyContinue
    Get-ChildItem -Path (Join-Path $ProjectRoot 'release') -Filter '*.obj' -ErrorAction SilentlyContinue | Remove-Item -Force
    Get-ChildItem -Path (Join-Path $ProjectRoot 'release') -Filter '*.pdb' -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like '*Setup*.pdb' -or $_.Name -like 'Vertical*' } |
        Remove-Item -Force -ErrorAction SilentlyContinue
    Log-Group
}

function Package {
    trap {
        Write-Error $_
        exit 2
    }

    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."
    $BuildSpecFile = "${ProjectRoot}/buildspec.json"

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse
    foreach( $Utility in $UtilityFunctions ) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    $BuildSpec = Get-Content -Path ${BuildSpecFile} -Raw | ConvertFrom-Json
    $ProductName = $BuildSpec.name
    $ProductVersion = $BuildSpec.version
    $DisplayName = if ($BuildSpec.displayName) { [string]$BuildSpec.displayName } else { 'Vertical Shorts Plugin' }

    # Official public artifact basenames (no extension):
    #   Vertical Shorts Plugin 1.0.5.zip
    #   Vertical Shorts Plugin 1.0.5 Setup.exe
    $OutputName = "${ProductName}-${ProductVersion}-windows-${Target}"
    $OfficialZipBase = "${DisplayName} ${ProductVersion}"
    $SetupName = "${DisplayName} ${ProductVersion} Setup"

    $ReleaseDir = "${ProjectRoot}/release/${Configuration}"

    if (Test-Path "${ProjectRoot}/INSTALL-WINDOWS.txt") {
        Copy-Item -Force "${ProjectRoot}/INSTALL-WINDOWS.txt" "${ReleaseDir}/INSTALL.txt"
    }

    Get-ChildItem -Path $ReleaseDir -Recurse -Filter *.pdb -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue

    $RemoveArgs = @{
        ErrorAction = 'SilentlyContinue'
        Path = @(
            "${ProjectRoot}/release/${ProductName}-*-windows-*.zip"
            "${ProjectRoot}/release/Vertical-Shorts-Plugin*.zip"
            "${ProjectRoot}/release/Vertical-Shorts-Plugin-Setup.exe"
            "${ProjectRoot}/release/Vertical Shorts Plugin*.zip"
            "${ProjectRoot}/release/Vertical Shorts Plugin*Setup.exe"
            "${ProjectRoot}/release/VerticalShortsPlugin-*"
            "${ProjectRoot}/release/ShortsVertical-*"
            "${ProjectRoot}/release/Package"
            "${ProjectRoot}/release/setup-build"
        )
    }
    Remove-Item @RemoveArgs -Recurse

    Log-Group "Archiving ${DisplayName} zip package..."
    $CompressArgs = @{
        Path = (Get-ChildItem -Path $ReleaseDir -Exclude "${OutputName}*.*", "${OfficialZipBase}*.*", "*.exe")
        CompressionLevel = 'Optimal'
        DestinationPath = "${ProjectRoot}/release/${OutputName}.zip"
        Verbose = ($Env:CI -ne $null)
    }
    Compress-Archive -Force @CompressArgs
    # Official public zip name (spaces, versioned)
    Copy-Item -Force "${ProjectRoot}/release/${OutputName}.zip" "${ProjectRoot}/release/${OfficialZipBase}.zip"
    Log-Group

    Build-CleanSetupExe -ProjectRoot $ProjectRoot -ReleaseDir $ReleaseDir -ProductVersion $ProductVersion -SetupName $SetupName
}

Package
