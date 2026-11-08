# Glance uninstaller
param(
  [string]$InstallDir = "$env:LOCALAPPDATA\Glance"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "SilentlyContinue"

Write-Host "[glance] Uninstalling..." -ForegroundColor Cyan

Get-Process Glance -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

$startup = [Environment]::GetFolderPath("Startup")
$lnk = Join-Path $startup "Glance.lnk"
if (Test-Path $lnk) { Remove-Item $lnk -Force; Write-Host "Removed $lnk" }

$exe = Join-Path $InstallDir "Glance.exe"
if (Test-Path $exe) { Remove-Item $exe -Force }

$iniPortable = @(
  "$InstallDir\glance.ini",
  "$env:APPDATA\Glance\glance.ini"
)
foreach ($p in $iniPortable) {
  if (Test-Path $p) { Write-Host "Config left at $p (delete manually if desired)" -ForegroundColor Yellow }
}

# Try remove dir if empty
if (Test-Path $InstallDir) {
  $items = Get-ChildItem $InstallDir
  if (-not $items) { Remove-Item $InstallDir -Force; Write-Host "Removed $InstallDir" }
  else { Write-Host "$InstallDir not empty — kept ($($items.Count) items)" }
}

Write-Host "[glance] Uninstalled. AppBar reservation released." -ForegroundColor Cyan
