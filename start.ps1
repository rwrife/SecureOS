# start.ps1 - SecureOS one-command launcher (Windows)
#
# Clone the repo, run this script, and SecureOS boots in QEMU.
# Handles dependency setup, build, and boot in one step.
#
# Usage:
#   .\start.ps1 [-SetupOnly] [-BuildOnly] [-Graphics] [-SkipSetup] [-Clean]

[CmdletBinding()]
param(
  [switch]$SetupOnly,
  [switch]$BuildOnly,
  [switch]$Graphics,
  [switch]$SkipSetup,
  [switch]$Clean,
  [switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# Resolve $RootDir — if running inside a worktree, resolve to the main
# repository root so Docker mounts and relative paths work correctly.
$RootDir = $PSScriptRoot
$gitPath = Join-Path $RootDir ".git"
if ((Test-Path $gitPath) -and (-not (Test-Path $gitPath -PathType Container))) {
  # .git is a file (worktree pointer) — resolve the main repo root
  $gitContent = Get-Content $gitPath -Raw
  if ($gitContent -match 'gitdir:\s*(.+)') {
    $gitDir = $Matches[1].Trim()
    # gitdir points to .git/worktrees/<name> — go up 3 levels to get repo root
    $RootDir = (Resolve-Path (Join-Path $gitDir "..\..\..")).Path
    Write-Host "  (worktree detected, using repo root: $RootDir)" -ForegroundColor DarkGray
  }
}

if ($Help) {
  Write-Host @"
Usage: .\start.ps1 [OPTIONS]

Options:
  -SetupOnly   Install dependencies only (don't build or boot)
  -BuildOnly   Build the OS but don't boot it
  -Graphics    Boot with VGA display window instead of serial console
  -SkipSetup   Skip dependency checks (assumes Docker + QEMU installed)
  -Clean       Remove artifacts before building
  -Help        Show this help message
"@
  exit 0
}

# --- Banner ---
Write-Host ""
Write-Host "+==============================================+"
Write-Host "|            SecureOS Launcher                 |"
Write-Host "+==============================================+"
Write-Host ""

# --- Step 1: Dependency checks ---
if (-not $SkipSetup) {
  Write-Host "[1/3] Checking dependencies..."

  $needSetup = $false

  # Check Docker
  $hasDocker = $false
  if (Get-Command docker -ErrorAction SilentlyContinue) {
    $null = docker info 2>&1
    if ($LASTEXITCODE -eq 0) {
      Write-Host "  [OK] Docker" -ForegroundColor Green
      $hasDocker = $true
    } else {
      Write-Host "  [X] Docker installed but daemon not running" -ForegroundColor Red
      $needSetup = $true
    }
  } else {
    Write-Host "  [X] Docker not found" -ForegroundColor Red
    $needSetup = $true
  }

  # Check QEMU
  $hasQemu = $false
  if (Get-Command "qemu-system-x86_64" -ErrorAction SilentlyContinue) {
    Write-Host "  [OK] QEMU" -ForegroundColor Green
    $hasQemu = $true
  } elseif (Test-Path "C:\Program Files\qemu\qemu-system-x86_64.exe") {
    Write-Host "  [OK] QEMU (found in Program Files)" -ForegroundColor Green
    $hasQemu = $true
  } else {
    Write-Host "  [X] QEMU not found" -ForegroundColor Red
    $needSetup = $true
  }

  if ($needSetup) {
    Write-Host ""
    $response = Read-Host "Missing dependencies. Run setup script? [Y/n]"
    if ([string]::IsNullOrEmpty($response)) { $response = "Y" }

    if ($response -match '^[Yy]') {
      $setupScript = Join-Path $RootDir 'scripts\setup-windows.ps1'
      & $setupScript
    } else {
      Write-Host "Setup skipped. Install Docker Desktop and QEMU manually, then re-run."
      exit 1
    }
  }
  Write-Host ""
} else {
  Write-Host "[1/3] Skipping dependency checks (-SkipSetup)"
  Write-Host ""
}

if ($SetupOnly) {
  Write-Host "Setup complete. Dependencies are installed."
  exit 0
}

# --- Step 2: Build ---
Write-Host "[2/3] Building SecureOS..."

if ($Clean) {
  Write-Host "  Cleaning artifacts..."
  $artifactsDir = Join-Path $RootDir "artifacts"
  if (Test-Path $artifactsDir) {
    Remove-Item $artifactsDir -Recurse -Force
  }
}

$buildScript = Join-Path $RootDir 'scripts\build.ps1'
& $buildScript "all"
Write-Host ""

if ($BuildOnly) {
  Write-Host "Build complete. Artifacts in artifacts/"
  exit 0
}

# --- Step 3: Boot ---
Write-Host "[3/3] Booting SecureOS in QEMU..."
Write-Host ""

if ($Graphics) { $bootMode = "graphics" } else { $bootMode = "console" }
$bootScript = Join-Path $RootDir 'scripts\boot.ps1'
& $bootScript $bootMode
