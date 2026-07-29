[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$wingetCommand = Get-Command winget -ErrorAction SilentlyContinue
if ($null -eq $wingetCommand) {
    throw 'Windows Package Manager (winget) is required to install Typst.'
}

& $wingetCommand.Source install `
    --id Typst.Typst `
    --exact `
    --accept-package-agreements `
    --accept-source-agreements `
    --silent `
    --disable-interactivity

if ($LASTEXITCODE -ne 0) {
    throw "Typst installation exited with code $LASTEXITCODE."
}

Write-Host 'Typst installation completed. Open a new terminal if typst is not yet on PATH.'

