# Glance installer — one-liner friendly
param(
  [string]$BinUrl = "",                  # optional: URL to Glance.exe release
  [string]$InstallDir = "$env:LOCALAPPDATA\Glance",
  [switch]$NoStartup,
  [switch]$Portable
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Info($m){ Write-Host "[glance] $m" -ForegroundColor Cyan }

$exeName = "Glance.exe"
$dest = Join-Path $InstallDir $exeName

# Resolve source exe
$src = $null
# 1) If BinUrl given, download
if ($BinUrl) {
  $tmp = Join-Path $env:TEMP "Glance.exe"
  Write-Info "Downloading from $BinUrl ..."
  Invoke-WebRequest -Uri $BinUrl -OutFile $tmp -UseBasicParsing
  $src = $tmp
} else {
  # 2) Look for built binary in common locations
  $candidates = @(
    "$PSScriptRoot\..\build\Release\Glance.exe",
    "$PSScriptRoot\..\build\Glance.exe",
    "$PSScriptRoot\..\out\build\windows-x64-release\Glance.exe",
    ".\build\Release\Glance.exe",
    ".\Glance.exe"
  )
  foreach ($c in $candidates) {
    $p = Resolve-Path $c -ErrorAction SilentlyContinue
    if ($p) { $src = $p.Path; break }
  }
  if (-not $src) {
    # 3) Check latest GitHub release (if repo has one)
    try {
      Write-Info "No local build — checking GitHub releases..."
      $rel = Invoke-RestMethod -Uri "https://api.github.com/repos/Vorta/Loadbar/releases/latest" -UseBasicParsing -ErrorAction Stop
      # fallback: try this repo's releases (adjust org if forked)
      $apiUrl = "https://api.github.com/repos/$($env:GITHUB_REPO ?? 'Vorta/Loadbar')/releases/latest"
    } catch {}
    Write-Host "Build the project first: cmake -B build -DGLANCE_TINY=ON; cmake --build build --config Release" -ForegroundColor Yellow
    Write-Host "Then re-run this installer, or pass -BinUrl <url>" -ForegroundColor Yellow
    exit 1
  }
}

New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null
Write-Info "Installing to $dest (from $src)"
# Kill running instance
Get-Process Glance -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300
Copy-Item -Force -Path $src -Destination $dest

if (-not $Portable -and -not $NoStartup) {
  $startup = [Environment]::GetFolderPath("Startup")
  $lnkPath = Join-Path $startup "Glance.lnk"
  $wsh = New-Object -ComObject WScript.Shell
  $lnk = $wsh.CreateShortcut($lnkPath)
  $lnk.TargetPath = $dest
  $lnk.WorkingDirectory = $InstallDir
  $lnk.Description = "Glance — lightweight system monitor"
  $lnk.Save()
  Write-Info "Autostart shortcut: $lnkPath"
}

Write-Info "Starting Glance..."
Start-Process $dest

Write-Info "Done — tray icon should appear. Right-click to exit."
