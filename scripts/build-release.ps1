[CmdletBinding()]
param(
    [string]$Version
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$companionDirectory = Join-Path $repositoryRoot 'Software\Companion'
$gatewayDirectory = Join-Path $repositoryRoot 'Software\Gateway'
$masterDirectory = Join-Path $repositoryRoot 'Software\Master'
$releaseScript = Join-Path $PSScriptRoot 'release\flash-release.ps1'
$npm = 'C:\Program Files\nodejs\npm.cmd'
$platformio = 'C:\Users\Bram\.platformio\penv\Scripts\platformio.exe'

if (-not (Test-Path -LiteralPath $npm)) { throw "Node.js npm was not found at $npm." }
if (-not (Test-Path -LiteralPath $platformio)) { throw "PlatformIO was not found at $platformio." }
if (-not (Test-Path -LiteralPath $releaseScript)) { throw "Release flash script was not found at $releaseScript." }

function Assert-SemanticVersion([string]$Candidate) {
    if ($Candidate -notmatch '^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$') {
        throw "Version '$Candidate' must use semantic version format, for example 0.1.1 or 0.2.0-rc.1."
    }
}

if ($null -eq ('FlexBms.ReleaseCrc32' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.IO;
namespace FlexBms {
    public static class ReleaseCrc32 {
        public static string FileHex(string path) {
            uint crc = 0xffffffffU;
            foreach (byte value in File.ReadAllBytes(path)) {
                crc ^= value;
                for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) != 0 ? 0xedb88320U : 0U);
            }
            return (crc ^ 0xffffffffU).ToString("X8");
        }
    }
}
'@
}

function Write-OtaManifest([string]$Target, [string]$ImageName, [string]$ManifestName) {
    $imagePath = Join-Path $releaseDirectory $ImageName
    $manifest = [ordered]@{
        target = $Target
        version = $Version
        image_file = $ImageName
        image_bytes = (Get-Item -LiteralPath $imagePath).Length
        image_crc32 = [FlexBms.ReleaseCrc32]::FileHex($imagePath)
    }
    $manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $releaseDirectory $ManifestName) -Encoding utf8
}

$packagePath = Join-Path $companionDirectory 'package.json'
$package = Get-Content -Raw $packagePath | ConvertFrom-Json
$currentVersion = [string]$package.version
Assert-SemanticVersion $currentVersion
$versionWasProvided = $PSBoundParameters.ContainsKey('Version')

if ($versionWasProvided) {
    $Version = $Version.Trim()
}
else {
    $enteredVersion = Read-Host "Release version [$currentVersion] (Enter to use it, or type a new version)"
    $Version = if ([string]::IsNullOrWhiteSpace($enteredVersion)) { $currentVersion } else { $enteredVersion.Trim() }
}
Assert-SemanticVersion $Version

$repeatRelease = $false
$releaseBaseDirectory = Join-Path $repositoryRoot "release\FlexBMS-$Version"
while (Test-Path -LiteralPath $releaseBaseDirectory) {
    if ($versionWasProvided) {
        $repeatRelease = $true
        break
    }

    $enteredVersion = Read-Host "Release $Version already exists. Type a new version, R for a timestamped repeat, or press Enter to cancel"
    if ([string]::IsNullOrWhiteSpace($enteredVersion)) { throw 'Release cancelled.' }
    if ($enteredVersion.Trim() -ieq 'R') {
        $repeatRelease = $true
        break
    }

    $Version = $enteredVersion.Trim()
    Assert-SemanticVersion $Version
    $releaseBaseDirectory = Join-Path $repositoryRoot "release\FlexBMS-$Version"
}

if ($Version -ne $currentVersion) {
    Write-Host "Updating Companion release version from $currentVersion to $Version"
    Push-Location $companionDirectory
    try {
        & $npm version $Version --no-git-tag-version --ignore-scripts
        if ($LASTEXITCODE -ne 0) { throw 'Failed to update the Companion release version.' }
    }
    finally { Pop-Location }

    $package = Get-Content -Raw $packagePath | ConvertFrom-Json
    if ($package.version -ne $Version) { throw "Companion package version was not updated to $Version." }
}

$releaseDirectory = $releaseBaseDirectory
if ($repeatRelease -or (Test-Path -LiteralPath $releaseDirectory)) {
    $timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $releaseDirectory = "$releaseBaseDirectory-$timestamp"
    $duplicate = 2
    while (Test-Path -LiteralPath $releaseDirectory) {
        $releaseDirectory = "$releaseBaseDirectory-$timestamp-$duplicate"
        $duplicate++
    }
    Write-Host "Creating timestamped repeat release in $releaseDirectory"
}
$desktopBuildDirectory = Join-Path $env:TEMP "flexbms-desktop-$Version-$PID"

Push-Location $companionDirectory
try {
    & $npm run check
    if ($LASTEXITCODE -ne 0) { throw 'Companion checks failed.' }
    $previousDesktopOutput = $env:FLEXBMS_DESKTOP_OUTPUT_DIR
    $env:FLEXBMS_DESKTOP_OUTPUT_DIR = $desktopBuildDirectory
    & $npm run build:desktop
    if ($LASTEXITCODE -ne 0) { throw 'Portable Companion build failed.' }
}
finally {
    if ($null -eq $previousDesktopOutput) { Remove-Item Env:FLEXBMS_DESKTOP_OUTPUT_DIR -ErrorAction SilentlyContinue }
    else { $env:FLEXBMS_DESKTOP_OUTPUT_DIR = $previousDesktopOutput }
    Pop-Location
}

Push-Location $gatewayDirectory
try {
    & $platformio run
    if ($LASTEXITCODE -ne 0) { throw 'Gateway build failed.' }
}
finally { Pop-Location }

Push-Location $masterDirectory
try {
    & $platformio run
    if ($LASTEXITCODE -ne 0) { throw 'STM32 build failed.' }
}
finally { Pop-Location }

$desktopArtifact = Join-Path $desktopBuildDirectory "FlexBMS-Companion-$Version-portable.exe"
$gatewayArtifact = Join-Path $gatewayDirectory '.pio\build\flexbms_gateway\firmware.bin'
$gatewayBootloader = Join-Path $gatewayDirectory '.pio\build\flexbms_gateway\bootloader.bin'
$gatewayPartitions = Join-Path $gatewayDirectory '.pio\build\flexbms_gateway\partitions.bin'
$stm32Artifact = Join-Path $masterDirectory '.pio\build\default\firmware.bin'
foreach ($artifact in @($desktopArtifact, $gatewayArtifact, $gatewayBootloader, $gatewayPartitions, $stm32Artifact)) {
    if (-not (Test-Path -LiteralPath $artifact)) { throw "Expected release artifact was not produced: $artifact" }
}

New-Item -ItemType Directory -Path $releaseDirectory | Out-Null
Copy-Item -LiteralPath $desktopArtifact -Destination (Join-Path $releaseDirectory 'FlexBMS-Companion.exe')
Copy-Item -LiteralPath $gatewayArtifact -Destination (Join-Path $releaseDirectory 'FlexBMS-Gateway.bin')
Copy-Item -LiteralPath $stm32Artifact -Destination (Join-Path $releaseDirectory 'FlexBMS-STM32.bin')
Copy-Item -LiteralPath $releaseScript -Destination (Join-Path $releaseDirectory 'flash-release.ps1')

$esptool = Join-Path $env:USERPROFILE '.platformio\packages\tool-esptoolpy\esptool.py'
$platformioPython = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
if (-not (Test-Path -LiteralPath $esptool) -or -not (Test-Path -LiteralPath $platformioPython)) {
    throw 'PlatformIO esptool.py or its Python runtime was not found; cannot create the Gateway factory image.'
}
$gatewayFactoryArtifact = Join-Path $releaseDirectory 'FlexBMS-Gateway-factory.bin'
$gatewayPartitionCsv = Join-Path $gatewayDirectory 'partitions.csv'
$gatewayAppOffset = $null
foreach ($line in Get-Content -LiteralPath $gatewayPartitionCsv) {
    $fields = $line.Split(',') | ForEach-Object { $_.Trim() }
    if ($fields.Count -ge 4 -and $fields[0] -eq 'ota_0') {
        $gatewayAppOffset = $fields[3]
        break
    }
}
if ($gatewayAppOffset -notmatch '^0x[0-9A-Fa-f]+$') { throw 'Gateway partitions.csv must define ota_0 with an explicit hexadecimal offset.' }
& $platformioPython $esptool --chip esp32c3 merge_bin --flash_mode dio --flash_freq 80m --flash_size 4MB -o $gatewayFactoryArtifact 0x0 $gatewayBootloader 0x8000 $gatewayPartitions $gatewayAppOffset $gatewayArtifact
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $gatewayFactoryArtifact)) { throw 'Gateway factory image creation failed.' }

# The ESP32 bootloader reads applications from the OTA partition, not the
# ESP-IDF default 0x10000 offset.  Catch an incorrect merge immediately rather
# than producing a factory image that can only bootloop on the target.
$gatewayAppOffsetValue = [Convert]::ToInt32($gatewayAppOffset.Substring(2), 16)
$gatewayImageBytes = [System.IO.File]::ReadAllBytes($gatewayArtifact)
$gatewayFactoryBytes = [System.IO.File]::ReadAllBytes($gatewayFactoryArtifact)
if ($gatewayImageBytes.Length -eq 0 -or $gatewayImageBytes[0] -ne 0xE9) { throw 'Gateway application image does not have an ESP image magic byte.' }
if ($gatewayFactoryBytes.Length -le $gatewayAppOffsetValue -or $gatewayFactoryBytes[$gatewayAppOffsetValue] -ne $gatewayImageBytes[0]) {
    throw "Gateway factory image does not contain the application at ota_0 offset $gatewayAppOffset."
}

Write-OtaManifest 'gateway' 'FlexBMS-Gateway.bin' 'FlexBMS-Gateway.manifest.json'
Write-OtaManifest 'stm32' 'FlexBMS-STM32.bin' 'FlexBMS-STM32.manifest.json'

$hashes = Get-ChildItem -LiteralPath $releaseDirectory -File |
    Where-Object { $_.Name -ne 'SHA256SUMS.txt' } |
    ForEach-Object { "$(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256 | Select-Object -ExpandProperty Hash) *$($_.Name)" }
Set-Content -LiteralPath (Join-Path $releaseDirectory 'SHA256SUMS.txt') -Value $hashes
Write-Host "Release artifacts created in $releaseDirectory"
