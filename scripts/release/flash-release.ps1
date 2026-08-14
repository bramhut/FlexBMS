[CmdletBinding()]
param(
    [ValidateSet('All', 'Stm32', 'Gateway')]
    [string]$Target = 'All',
    [string]$GatewayPort
)

$ErrorActionPreference = 'Stop'
$releaseDirectory = $PSScriptRoot
$hashFile = Join-Path $releaseDirectory 'SHA256SUMS.txt'

if (-not (Test-Path -LiteralPath $hashFile)) { throw "SHA256SUMS.txt was not found in $releaseDirectory." }
foreach ($line in Get-Content -LiteralPath $hashFile) {
    $parts = $line -split ' \*', 2
    if ($parts.Count -ne 2) { throw "Malformed checksum entry: $line" }
    $path = Join-Path $releaseDirectory $parts[1]
    if (-not (Test-Path -LiteralPath $path)) { throw "Release artifact is missing: $path" }
    $actual = Get-FileHash -LiteralPath $path -Algorithm SHA256 | Select-Object -ExpandProperty Hash
    if ($actual -ne $parts[0]) { throw "Checksum verification failed for $($parts[1])." }
}

function Find-Stm32Programmer {
    $candidates = @(
        $env:STM32_PROGRAMMER_CLI,
        'C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe',
        'C:\Program Files (x86)\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe'
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    throw 'STM32CubeProgrammer CLI was not found. Install it or set STM32_PROGRAMMER_CLI to STM32_Programmer_CLI.exe.'
}

function Find-EspTool {
    $scriptCandidates = @(
        $env:FLEXBMS_ESPTOOL,
        (Join-Path $env:USERPROFILE '.platformio\packages\tool-esptoolpy\esptool.py')
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    foreach ($candidate in $scriptCandidates) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    throw 'esptool.py was not found. Install PlatformIO or set FLEXBMS_ESPTOOL to esptool.py.'
}

function Invoke-Stm32Flash {
    $image = Join-Path $releaseDirectory 'FlexBMS-STM32.bin'
    $programmer = Find-Stm32Programmer
    Write-Host 'Flashing STM32 application through ST-Link/SWD.'
    & $programmer -c port=SWD mode=UR -w $image 0x08000000 -v -rst
    if ($LASTEXITCODE -ne 0) { throw 'STM32 flash or verification failed.' }
}

function Invoke-GatewayFlash {
    $image = Join-Path $releaseDirectory 'FlexBMS-Gateway-factory.bin'
    if ([string]::IsNullOrWhiteSpace($GatewayPort)) {
        $GatewayPort = Read-Host 'ESP32-C3 serial port (for example COM6)'
    }
    if ($GatewayPort -notmatch '^COM[0-9]+$') { throw 'Gateway port must be a Windows COM port, for example COM6.' }
    $esptool = Find-EspTool
    $python = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
    if (-not (Test-Path -LiteralPath $python)) { $python = 'py' }
    $pythonArguments = @()
    if ($python -eq 'py') { $pythonArguments += '-3' }
    $pythonArguments += @($esptool, '--chip', 'esp32c3', '--port', $GatewayPort, '--baud', '460800', '--before', 'default_reset', '--after', 'hard_reset', 'write_flash', '-z', '--flash_mode', 'dio', '--flash_freq', '80m', '--flash_size', '4MB', '0x0', $image)
    Write-Host "Flashing complete Gateway factory image through $GatewayPort."
    & $python @pythonArguments
    if ($LASTEXITCODE -ne 0) { throw 'Gateway flash or verification failed.' }
}

if ($Target -in @('All', 'Stm32')) { Invoke-Stm32Flash }
if ($Target -in @('All', 'Gateway')) { Invoke-GatewayFlash }
Write-Host 'Requested release flashing completed successfully.'
