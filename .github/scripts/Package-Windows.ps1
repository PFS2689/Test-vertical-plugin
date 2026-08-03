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

function Package {
    trap {
        Write-Error $_
        exit 2
    }

    $ScriptHome = $PSScriptRoot
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

    $OutputName = "${ProductName}-${ProductVersion}-windows-${Target}"
    # Manual (non one-click) zip downloads — Vertical Shorts Plugin naming
    $ManualZipVersioned = "Vertical-Shorts-Plugin-${ProductVersion}"
    $ManualZipStable = "Vertical-Shorts-Plugin"
    $FriendlySetup = "VerticalShortsPlugin-Setup"
    $VersionedSetup = "VerticalShortsPlugin-${ProductVersion}-Windows-Setup"

    $ReleaseDir = "${ProjectRoot}/release/${Configuration}"

    if (Test-Path "${ProjectRoot}/INSTALL-WINDOWS.txt") {
        Copy-Item -Force "${ProjectRoot}/INSTALL-WINDOWS.txt" "${ReleaseDir}/INSTALL.txt"
    }

    $RemoveArgs = @{
        ErrorAction = 'SilentlyContinue'
        Path = @(
            "${ProjectRoot}/release/${ProductName}-*-windows-*.zip"
            "${ProjectRoot}/release/Vertical-Shorts-Plugin*.zip"
            "${ProjectRoot}/release/VerticalShortsPlugin-*"
            "${ProjectRoot}/release/ShortsVertical-*"
            "${ProjectRoot}/release/Package"
        )
    }
    Remove-Item @RemoveArgs -Recurse

    Log-Group "Archiving ${ProductName} (manual zip install)..."
    $CompressArgs = @{
        Path = (Get-ChildItem -Path $ReleaseDir -Exclude "${OutputName}*.*", "${ManualZipVersioned}*.*", "${ManualZipStable}*.*", "*.exe")
        CompressionLevel = 'Optimal'
        DestinationPath = "${ProjectRoot}/release/${OutputName}.zip"
        Verbose = ($Env:CI -ne $null)
    }
    Compress-Archive -Force @CompressArgs
    # Versioned + stable names for /releases/latest/download/Vertical-Shorts-Plugin.zip
    Copy-Item -Force "${ProjectRoot}/release/${OutputName}.zip" "${ProjectRoot}/release/${ManualZipVersioned}.zip"
    Copy-Item -Force "${ProjectRoot}/release/${OutputName}.zip" "${ProjectRoot}/release/${ManualZipStable}.zip"
    Log-Group

    $IsccFile = "${ProjectRoot}/build_${Target}/installer-Windows.iss"
    if ( ! ( Test-Path -Path $IsccFile ) ) {
        throw "InnoSetup script not found at ${IsccFile}. Build the project first."
    }

    $iscc = Get-Command iscc -ErrorAction SilentlyContinue
    if ( -not $iscc ) {
        $candidates = @(
            "${Env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
            "${Env:LocalAppData}\Programs\Inno Setup 6\ISCC.exe"
        )
        foreach ($c in $candidates) {
            if (Test-Path $c) {
                $iscc = $c
                break
            }
        }
    } else {
        $iscc = $iscc.Source
    }

    if ( -not $iscc ) {
        throw "Inno Setup compiler (iscc) not found. Install Inno Setup 6."
    }

    Log-Group "Creating one-click Windows installer..."
    Push-Location -Stack BuildTemp
    Ensure-Location -Path "${ProjectRoot}/release"

    Copy-Item -Path $Configuration -Destination Package -Recurse
    if (Test-Path "Package/INSTALL.txt") {
        Remove-Item -Force "Package/INSTALL.txt"
    }

    Invoke-External $iscc $IsccFile "/O${ProjectRoot}/release" "/F${VersionedSetup}"

    Remove-Item -Path Package -Recurse -Force
    Pop-Location -Stack BuildTemp

    # Stable filename for /releases/latest/download/VerticalShortsPlugin-Setup.exe
    if (Test-Path "${ProjectRoot}/release/${VersionedSetup}.exe") {
        Copy-Item -Force "${ProjectRoot}/release/${VersionedSetup}.exe" `
            "${ProjectRoot}/release/${FriendlySetup}.exe"
    } else {
        throw "Installer exe was not created: ${VersionedSetup}.exe"
    }
    Log-Group
}

Package
