# setup-windows.ps1 - Install Docker Desktop and QEMU on Windows
#
# This script installs the two host dependencies needed to build and run
# SecureOS: Docker Desktop and QEMU. All other toolchain tools (compiler,
# linker, etc.) live inside the Docker container.
#
# Run from an elevated (Administrator) PowerShell prompt if installing.

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Write-Ok   { param([string]$Msg) Write-Host "  [OK] $Msg" -ForegroundColor Green }
function Write-Err  { param([string]$Msg) Write-Host "  [X] $Msg" -ForegroundColor Red }
function Write-Step { param([string]$Msg) Write-Host "`n==> $Msg" }

function Test-Command {
  param([string]$Name)
  $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Find-Qemu {
  if (Test-Command "qemu-system-x86_64") { return $true }
  $commonPaths = @(
    "C:\Program Files\qemu\qemu-system-x86_64.exe",
    "C:\Program Files (x86)\qemu\qemu-system-x86_64.exe"
  )
  foreach ($p in $commonPaths) {
    if (Test-Path $p) { return $true }
  }
  return $false
}

function Install-Docker {
  Write-Step "Installing Docker Desktop"

  if (Test-Command "winget") {
    Write-Host "  Using winget..."
    winget install --id Docker.DockerDesktop --accept-source-agreements --accept-package-agreements
  } elseif (Test-Command "choco") {
    Write-Host "  Using chocolatey..."
    choco install docker-desktop -y
  } else {
    Write-Err "Neither winget nor choco found."
    Write-Host "  Install Docker Desktop manually: https://www.docker.com/products/docker-desktop/"
    Write-Host "  Or install winget: https://aka.ms/getwinget"
    exit 1
  }

  Write-Host ""
  Write-Host "  NOTE: Docker Desktop may require a restart."
  Write-Host "  After restart, ensure Docker Desktop is running, then re-run this script to verify."
}

function Install-Qemu {
  Write-Step "Installing QEMU"

  if (Test-Command "winget") {
    Write-Host "  Using winget..."
    winget install --id QEMU.QEMU --accept-source-agreements --accept-package-agreements
  } elseif (Test-Command "choco") {
    Write-Host "  Using chocolatey..."
    choco install qemu -y
  } else {
    Write-Err "Neither winget nor choco found."
    Write-Host "  Install QEMU manually: https://www.qemu.org/download/#windows"
    exit 1
  }

  # Add QEMU to PATH for this session
  $qemuPath = "C:\Program Files\qemu"
  if ((Test-Path $qemuPath) -and ($env:PATH -notlike "*$qemuPath*")) {
    $env:PATH = "$qemuPath;$env:PATH"
  }
}

function Test-DockerRunning {
  try {
    $null = docker info 2>&1
    return ($LASTEXITCODE -eq 0)
  } catch {
    return $false
  }
}

function Verify-Installation {
  Write-Step "Verifying installation"
  $failed = $false

  if (Test-Command "docker") {
    $ver = docker --version 2>&1
    Write-Ok "Docker installed: $ver"
  } else {
    Write-Err "Docker not found in PATH"
    $failed = $true
  }

  if (Find-Qemu) {
    if (Test-Command "qemu-system-x86_64") {
      $ver = qemu-system-x86_64 --version 2>&1 | Select-Object -First 1
      Write-Ok "QEMU installed: $ver"
    } else {
      Write-Ok "QEMU found at C:\Program Files\qemu\"
      Write-Host "    Add to PATH: `$env:PATH += ';C:\Program Files\qemu'"
    }
  } else {
    Write-Err "QEMU not found"
    $failed = $true
  }

  if (Test-DockerRunning) {
    Write-Ok "Docker daemon is running"
  } else {
    Write-Host "  [!] Docker daemon not running. Start Docker Desktop first." -ForegroundColor Yellow
  }

  if ($failed) {
    Write-Host ""
    Write-Host "Some installations failed. See messages above." -ForegroundColor Red
    exit 1
  }

  Write-Host ""
  Write-Host "[OK] Setup complete! You can now run: .\start.ps1" -ForegroundColor Green
}

# --- Main ---
Write-Step "SecureOS Windows Setup"

# Docker
if (Test-Command "docker") {
  Write-Ok "Docker already installed"
} else {
  Install-Docker
}

# QEMU
if (Find-Qemu) {
  Write-Ok "QEMU already installed"
} else {
  Install-Qemu
}

Verify-Installation
