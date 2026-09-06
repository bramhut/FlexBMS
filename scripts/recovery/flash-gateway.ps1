[CmdletBinding()]
param(
    [string]$Port
)

$ErrorActionPreference = 'Stop'
$packageDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$imagePath = Join-Path $packageDirectory 'FlexBMS-Gateway-factory.bin'
$checksumPath = Join-Path $packageDirectory 'SHA256SUMS.txt'

function Fail([string]$Message) {
    Write-Host "`nERROR: $Message" -ForegroundColor Red
    throw $Message
}

if (-not (Test-Path -LiteralPath $imagePath)) { Fail "Firmware image not found: $imagePath" }
if (-not (Test-Path -LiteralPath $checksumPath)) { Fail "Checksum file not found: $checksumPath" }

$checksumLine = Get-Content -LiteralPath $checksumPath |
    Where-Object { $_ -match '\*FlexBMS-Gateway-factory\.bin$' } |
    Select-Object -First 1
if ([string]::IsNullOrWhiteSpace($checksumLine)) { Fail 'The checksum file has no Gateway image entry.' }
$expectedHash = ($checksumLine -split '\s+', 2)[0].ToUpperInvariant()
$actualHash = (Get-FileHash -LiteralPath $imagePath -Algorithm SHA256).Hash.ToUpperInvariant()
if ($actualHash -ne $expectedHash) {
    Fail "Firmware checksum mismatch. Expected $expectedHash, got $actualHash."
}

function Get-PythonLauncher {
    $py = Get-Command py.exe -ErrorAction SilentlyContinue
    if ($null -ne $py) {
        try {
            & $py.Source -3 --version *> $null
            if ($LASTEXITCODE -eq 0) { return @{ exe = $py.Source; prefix = @('-3') } }
        } catch { }
    }

    $python = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($null -ne $python) {
        try {
            & $python.Source --version *> $null
            if ($LASTEXITCODE -eq 0) { return @{ exe = $python.Source; prefix = @() } }
        } catch { }
    }

    $localPython = Get-ChildItem -LiteralPath (Join-Path $env:LOCALAPPDATA 'Programs\Python') -Filter python.exe -Recurse -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | Select-Object -First 1
    if ($null -ne $localPython) {
        return @{ exe = $localPython.FullName; prefix = @() }
    }

    return $null
}

$pythonLauncher = Get-PythonLauncher
if ($null -eq $pythonLauncher) {
    $winget = Get-Command winget.exe -ErrorAction SilentlyContinue
    if ($null -eq $winget) {
        Fail 'Python is not installed and Windows winget is unavailable. Install Python 3 from python.org, then run this script again.'
    }

    Write-Host 'Python is not installed. Installing a per-user Python runtime through winget...' -ForegroundColor Yellow
    & $winget.Source install --id Python.Python.3.12 --exact --scope user --accept-source-agreements --accept-package-agreements
    if ($LASTEXITCODE -ne 0) { Fail 'Python installation through winget failed.' }
    $pythonLauncher = Get-PythonLauncher
    if ($null -eq $pythonLauncher) { Fail 'Python was installed but could not be located. Close and reopen this window, then run again.' }
}

function Invoke-Python([string[]]$Arguments) {
    & $pythonLauncher.exe @($pythonLauncher.prefix + $Arguments)
    return $LASTEXITCODE
}

$esptoolCommand = Get-Command esptool.exe -ErrorAction SilentlyContinue
if ($null -eq $esptoolCommand) { $esptoolCommand = Get-Command esptool -ErrorAction SilentlyContinue }
if ($null -eq $esptoolCommand) {
    Write-Host 'Installing esptool 4.9.0 for this recovery session...' -ForegroundColor Yellow
    $pipExit = Invoke-Python @('-m', 'pip', 'install', '--user', 'esptool==4.9.0')
    if ($pipExit -ne 0) { Fail 'Could not install esptool. Check the laptop internet connection and try again.' }
}

function Invoke-Esptool([string[]]$Arguments) {
    if ($null -ne $esptoolCommand) {
        & $esptoolCommand.Source @Arguments
    }
    else {
        & $pythonLauncher.exe @($pythonLauncher.prefix + @('-m', 'esptool') + $Arguments)
    }
    return $LASTEXITCODE
}

if ([string]::IsNullOrWhiteSpace($Port)) {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object {
        [int]($_ -replace '^COM', '')
    }
    if ($ports.Count -eq 0) {
        Fail 'No COM ports were found. Connect a data-capable USB cable to the ESP32-C3 and check Windows Device Manager.'
    }
    if ($ports.Count -eq 1) {
        $Port = $ports[0]
        Write-Host "Using detected serial port $Port."
    }
    else {
        Write-Host "`nDetected COM ports:"
        for ($index = 0; $index -lt $ports.Count; $index++) {
            Write-Host "  $($index + 1): $($ports[$index])"
        }
        $selection = Read-Host 'Enter the number of the ESP32-C3 port (or type COMx)'
        if ($selection -match '^COM[0-9]+$') {
            $Port = $selection.ToUpperInvariant()
        }
        elseif ($selection -match '^[0-9]+$' -and [int]$selection -ge 1 -and [int]$selection -le $ports.Count) {
            $Port = $ports[[int]$selection - 1]
        }
        else {
            Fail 'That is not a valid COM port selection.'
        }
    }
}
$Port = $Port.Trim().ToUpperInvariant()
if ($Port -notmatch '^COM[0-9]+$') { Fail "Invalid COM port '$Port'. Use a value such as COM6." }

Write-Host "`nVerified image: $([math]::Round((Get-Item -LiteralPath $imagePath).Length / 1MB, 2)) MiB"
Write-Host "Target: ESP32-C3 Gateway on $Port"
Write-Host 'The image contains the bootloader, partition table, and application.'
Write-Host 'Starting flash; do not disconnect the USB cable.' -ForegroundColor Cyan

$flashArguments = @(
    '--chip', 'esp32c3', '--port', $Port, '--baud', '460800',
    '--before', 'default_reset', '--after', 'hard_reset', 'write_flash', '-z',
    '--flash_mode', 'dio', '--flash_freq', '80m', '--flash_size', '4MB',
    '0x0', $imagePath
)
$flashExit = Invoke-Esptool $flashArguments
if ($flashExit -ne 0) {
    Write-Host "`nAutomatic reset into download mode did not work." -ForegroundColor Yellow
    Write-Host 'Hold BOOT, press and release RESET/EN, release BOOT, then press Enter.'
    [void](Read-Host 'Press Enter when the ESP32-C3 is in download mode')
    $flashExit = Invoke-Esptool $flashArguments
}
if ($flashExit -ne 0) {
    Fail 'Gateway flash failed. The image was not accepted by esptool.'
}

Write-Host "`nGateway flash and esptool verification completed successfully." -ForegroundColor Green
Write-Host 'Leave the USB cable connected for at least 60 seconds while the Gateway boots.'
Write-Host 'Then check http://flexbms.local or connect to FlexBMS-Setup-XXYYZZ and open http://192.168.4.1.'
