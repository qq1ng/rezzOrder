<#
.SYNOPSIS
  Builds everything, runs the unit tests, and re-renders the UI screenshots.
.DESCRIPTION
  One command to see whether the addon is still sound: the tracker/session/share tests must pass, and every
  turn window layout must still draw what it is supposed to (tools/uishot checks each render and writes
  docs/shots/report.txt, which shows up in git diff when a layout changes).
.EXAMPLE
  .\scripts\check.ps1
#>
param(
    [ValidateSet("release", "debug")] [string] $Preset = "release",
    [switch] $NoShots
)
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent

& "$PSScriptRoot\build.ps1" -Preset $Preset | Write-Host

Write-Host "`n--- unit tests ---"
& "$root\build\$Preset\rezz_tracker_tests.exe"
if ($LASTEXITCODE -ne 0) { throw "Unit tests failed." }

if (-not $NoShots) {
    Write-Host "`n--- UI renders ---"
    & "$root\build\$Preset\rezz_uishot.exe" --out "$root\docs\shots"
    if ($LASTEXITCODE -ne 0) { throw "UI renders failed." }
}

Write-Host "`nAll checks passed."
