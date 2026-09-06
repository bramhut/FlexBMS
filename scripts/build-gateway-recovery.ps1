[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
# Gateway's Vite build writes Unicode status markers. PlatformIO must inherit
# UTF-8 on Windows or its output reader can crash under the CP1252 default.
$env:PYTHONIOENCODING = 'utf-8'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$companionDirectory = Join-Path $repositoryRoot 'Software\Companion'
$gatewayDirectory = Join-Path $repositoryRoot 'Software\Gateway'
$packageJson = Join-Path $companionDirectory 'package.json'
$partitionCsv = Join-Path $gatewayDirectory 'partitions.csv'
$gatewayBuildDirectory = Join-Path $gatewayDirectory '.pio\build\flexbms_gateway'
$templateReadme = Join-Path $repositoryRoot 'Documentation\recovery\ESP32-Gateway-USB-Recovery-README.txt'
$templateNotes = Join-Path $repositoryRoot 'Documentation\recovery\ESP32-Gateway-USB-Recovery-NOTES.txt'
$templateFlashScript = Join-Path $PSScriptRoot 'recovery\flash-gateway.ps1'
$templateFlashLauncher = Join-Path $PSScriptRoot 'recovery\flash-gateway.cmd'

function Assert-SemanticVersion([string]$Candidate) {
    if ($Candidate -notmatch '^[0-9]+\.[0-9]+\.[0-9]+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$') {
        throw "Companion package version '$Candidate' is not a semantic version."
    }
}

function Get-Sha256FileHex([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

foreach ($requiredPath in @(
    $packageJson, $partitionCsv, $templateReadme, $templateNotes,
    $templateFlashScript, $templateFlashLauncher
)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required Gateway recovery input was not found: $requiredPath"
    }
}

$package = Get-Content -Raw -LiteralPath $packageJson | ConvertFrom-Json
$version = [string]$package.version
Assert-SemanticVersion $version

$platformioCoreDirectory = if ([string]::IsNullOrWhiteSpace($env:PLATFORMIO_CORE_DIR)) {
    Join-Path $env:USERPROFILE '.platformio'
} else {
    $env:PLATFORMIO_CORE_DIR
}
$platformioPackagesDirectory = if ([string]::IsNullOrWhiteSpace($env:PLATFORMIO_PACKAGES_DIR)) {
    Join-Path $platformioCoreDirectory 'packages'
} else {
    $env:PLATFORMIO_PACKAGES_DIR
}
$platformioCandidates = @(
    (Join-Path $platformioCoreDirectory 'penv\Scripts\platformio.exe')
    (Join-Path $platformioCoreDirectory 'penv\Scripts\pio.exe')
)
$platformioCommand = Get-Command platformio.exe -ErrorAction SilentlyContinue
$platformio = if ($null -ne $platformioCommand) {
    $platformioCommand.Source
} else {
    $platformioCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}
if ([string]::IsNullOrWhiteSpace($platformio)) {
    throw "PlatformIO was not found. Checked: $($platformioCandidates -join ', ')."
}
$npmCommand = Get-Command npm.cmd -ErrorAction SilentlyContinue
$npm = if ($null -ne $npmCommand) { $npmCommand.Source } else { 'C:\Program Files\nodejs\npm.cmd' }
if (-not (Test-Path -LiteralPath $npm)) {
    throw "Node.js npm was not found. Checked PATH and $npm."
}

$platformioPython = Join-Path $platformioCoreDirectory 'penv\Scripts\python.exe'
$esptool = Join-Path $platformioPackagesDirectory 'tool-esptoolpy\esptool.py'
if (-not (Test-Path -LiteralPath $platformioPython)) {
    throw "PlatformIO Python was not found: $platformioPython"
}
if (-not (Test-Path -LiteralPath $esptool)) {
    throw "PlatformIO esptool.py was not found: $esptool"
}

Write-Host "Running Companion checks for Gateway version $version..." -ForegroundColor Cyan
Push-Location $companionDirectory
try {
    & $npm run check
    if ($LASTEXITCODE -ne 0) { throw 'Companion checks failed.' }
}
finally {
    Pop-Location
}

Write-Host "Building FlexBMS Gateway version $version..." -ForegroundColor Cyan
Push-Location $gatewayDirectory
try {
    & $platformio run
    if ($LASTEXITCODE -ne 0) { throw 'Gateway build failed.' }
}
finally {
    Pop-Location
}

$gatewayFirmware = Join-Path $gatewayBuildDirectory 'firmware.bin'
$gatewayBootloader = Join-Path $gatewayBuildDirectory 'bootloader.bin'
$gatewayPartitions = Join-Path $gatewayBuildDirectory 'partitions.bin'
foreach ($artifact in @($gatewayFirmware, $gatewayBootloader, $gatewayPartitions)) {
    if (-not (Test-Path -LiteralPath $artifact)) {
        throw "Expected Gateway artifact was not produced: $artifact"
    }
}

$gatewayAppOffset = $null
foreach ($line in Get-Content -LiteralPath $partitionCsv) {
    $fields = $line.Split(',') | ForEach-Object { $_.Trim() }
    if ($fields.Count -ge 4 -and $fields[0] -eq 'ota_0') {
        $gatewayAppOffset = $fields[3]
        break
    }
}
if ($gatewayAppOffset -notmatch '^0x[0-9A-Fa-f]+$') {
    throw 'Gateway partitions.csv must define ota_0 with an explicit hexadecimal offset.'
}

$releaseRoot = Join-Path $repositoryRoot 'release'
$packageDirectory = Join-Path $releaseRoot "FlexBMS-Gateway-USB-Recovery-$version"
if (Test-Path -LiteralPath $packageDirectory) {
    $timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $packageDirectory = Join-Path $releaseRoot "FlexBMS-Gateway-USB-Recovery-$version-$timestamp"
    $duplicate = 2
    while (Test-Path -LiteralPath $packageDirectory) {
        $packageDirectory = Join-Path $releaseRoot "FlexBMS-Gateway-USB-Recovery-$version-$timestamp-$duplicate"
        $duplicate++
    }
}
New-Item -ItemType Directory -Path $packageDirectory -Force | Out-Null

$factoryImage = Join-Path $packageDirectory 'FlexBMS-Gateway-factory.bin'
Write-Host 'Creating merged 4 MiB factory image...' -ForegroundColor Cyan
& $platformioPython $esptool --chip esp32c3 merge_bin --flash_mode dio --flash_freq 80m --flash_size 4MB --fill-flash-size 4MB -o $factoryImage 0x0 $gatewayBootloader 0x8000 $gatewayPartitions $gatewayAppOffset $gatewayFirmware
if ($LASTEXITCODE -ne 0) { throw 'Gateway factory image creation failed.' }

$factoryBytes = [System.IO.File]::ReadAllBytes($factoryImage)
$gatewayImageBytes = [System.IO.File]::ReadAllBytes($gatewayFirmware)
$gatewayAppOffsetValue = [Convert]::ToInt32($gatewayAppOffset.Substring(2), 16)
if ($factoryBytes.Length -ne 4MB) { throw 'Gateway factory image is not exactly 4 MiB.' }
if ($gatewayImageBytes.Length -eq 0 -or $gatewayImageBytes[0] -ne 0xE9) {
    throw 'Gateway application image does not have an ESP image magic byte.'
}
if ($factoryBytes[$gatewayAppOffsetValue] -ne $gatewayImageBytes[0]) {
    throw "Gateway factory image does not contain the application at ota_0 offset $gatewayAppOffset."
}
for ($index = 0; $index -lt $gatewayImageBytes.Length; $index++) {
    if ($factoryBytes[$gatewayAppOffsetValue + $index] -ne $gatewayImageBytes[$index]) {
        throw ('Gateway factory image differs from the application at byte 0x{0:X}.' -f $index)
    }
}

Copy-Item -LiteralPath $templateReadme -Destination (Join-Path $packageDirectory 'README_FLASHING.txt')
Copy-Item -LiteralPath $templateNotes -Destination (Join-Path $packageDirectory 'RECOVERY_NOTES.txt')
Copy-Item -LiteralPath $templateFlashScript -Destination (Join-Path $packageDirectory 'flash-gateway.ps1')
Copy-Item -LiteralPath $templateFlashLauncher -Destination (Join-Path $packageDirectory 'flash-gateway.cmd')

$sourceCommit = (& git -C $repositoryRoot rev-parse HEAD 2>$null).Trim()
if ([string]::IsNullOrWhiteSpace($sourceCommit)) { $sourceCommit = 'unknown' }
$buildInfo = @"
FlexBMS Gateway USB recovery package

Gateway source commit: $sourceCommit
Companion/Gateway package version: $version
Target: ESP32-C3, 4 MiB flash
Factory image layout: bootloader 0x0, partition table 0x8000, ota_0 app $gatewayAppOffset
Flash command: write the factory image at 0x0
"@
Set-Content -LiteralPath (Join-Path $packageDirectory 'BUILD_INFO.txt') -Value $buildInfo -Encoding utf8

$checksumPath = Join-Path $packageDirectory 'SHA256SUMS.txt'
$hashLines = Get-ChildItem -LiteralPath $packageDirectory -File |
    Sort-Object Name |
    ForEach-Object { "$(Get-Sha256FileHex $_.FullName) *$($_.Name)" }
Set-Content -LiteralPath $checksumPath -Value $hashLines -Encoding ascii

foreach ($line in Get-Content -LiteralPath $checksumPath) {
    $parts = $line -split '\s+', 2
    if ($parts.Count -ne 2) { throw "Malformed generated checksum entry: $line" }
    $name = $parts[1].Substring(1)
    $actual = Get-Sha256FileHex (Join-Path $packageDirectory $name)
    if ($actual -ne $parts[0]) { throw "Generated checksum verification failed for $name." }
}

$zipPath = "$packageDirectory.zip"
Compress-Archive -LiteralPath $packageDirectory -DestinationPath $zipPath -CompressionLevel Optimal -Force

Add-Type -AssemblyName System.IO.Compression.FileSystem
$requiredEntries = @(
    'BUILD_INFO.txt', 'README_FLASHING.txt', 'RECOVERY_NOTES.txt',
    'SHA256SUMS.txt', 'flash-gateway.cmd', 'flash-gateway.ps1',
    'FlexBMS-Gateway-factory.bin'
)
$archive = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    $archiveNames = @($archive.Entries | ForEach-Object { Split-Path -Leaf $_.FullName })
    foreach ($requiredEntry in $requiredEntries) {
        if ($archiveNames -notcontains $requiredEntry) {
            throw "Generated ZIP is missing $requiredEntry."
        }
    }
}
finally {
    $archive.Dispose()
}

Write-Host "`nGateway recovery package created:" -ForegroundColor Green
Write-Host "  $zipPath"
Write-Host "  Factory image SHA-256: $(Get-Sha256FileHex $factoryImage)"
