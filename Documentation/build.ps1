[CmdletBinding()]
param(
    [switch]$Watch
)

$ErrorActionPreference = 'Stop'

$documentationDirectory = $PSScriptRoot
$sourcePath = Join-Path $documentationDirectory 'main.typ'
$outputPath = Join-Path $documentationDirectory 'FlexBMS-System-Overview.pdf'
$wingetAliasPath = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Links\typst.exe'

$typstCommand = Get-Command typst -ErrorAction SilentlyContinue
if ($null -ne $typstCommand) {
    $typstExecutable = $typstCommand.Source
}
elseif (Test-Path -LiteralPath $wingetAliasPath) {
    $typstExecutable = $wingetAliasPath
}
else {
    throw 'Typst is not installed. Run .\Documentation\install-typst.ps1 first.'
}

Write-Host "Using $(& $typstExecutable --version)"

if ($Watch) {
    & $typstExecutable watch $sourcePath $outputPath --root $documentationDirectory
}
else {
    & $typstExecutable compile $sourcePath $outputPath --root $documentationDirectory
}

if ($LASTEXITCODE -ne 0) {
    throw "Typst exited with code $LASTEXITCODE."
}

Write-Host "Generated $outputPath"
