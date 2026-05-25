# scripts/build.ps1 - Host-side build entry point (Windows)
#
# Invokes the Docker toolchain container to compile SecureOS.
# All compilation happens inside the container - this script just
# orchestrates the docker run.
#
# Usage: scripts\build.ps1 [kernel|disk|all]  (default: all)

[CmdletBinding()]
param(
  [Parameter(Position = 0)]
  [string]$Target = "all"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RootDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ImageTag = if ($env:SECUREOS_TOOLCHAIN_IMAGE) { $env:SECUREOS_TOOLCHAIN_IMAGE } else { "secureos/toolchain:bookworm-2026-02-12" }
$Dockerfile = Join-Path $RootDir "build\docker\Dockerfile.toolchain"

# Ensure Docker is available
if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
  Write-Host "ERROR: Docker is required. Run .\scripts\setup-windows.ps1" -ForegroundColor Red
  exit 1
}

$null = docker info 2>&1
if ($LASTEXITCODE -ne 0) {
  Write-Host "ERROR: Docker daemon is not running. Start Docker Desktop." -ForegroundColor Red
  exit 1
}

# Build toolchain image if it doesn't exist
docker image inspect $ImageTag *> $null
if ($LASTEXITCODE -ne 0) {
  Write-Host "Building toolchain image: $ImageTag"
  docker build -f $Dockerfile -t $ImageTag $RootDir
  if ($LASTEXITCODE -ne 0) { throw "Failed to build toolchain image" }
}

# Convert Windows path to Docker-compatible mount path
$DockerRoot = $RootDir -replace '\\', '/'

# Run the build inside the container
Write-Host "Building SecureOS (target: $Target)..."
docker run --rm -v "${DockerRoot}:/workspace" -w /workspace $ImageTag bash -lc "set -euo pipefail; ./build/scripts/build.sh $Target"
if ($LASTEXITCODE -ne 0) { throw "Build failed with exit code $LASTEXITCODE" }

Write-Host ""
Write-Host "✓ Build complete. Artifacts:" -ForegroundColor Green
$isoPath = Join-Path $RootDir "artifacts\kernel\secureos.iso"
$elfPath = Join-Path $RootDir "artifacts\kernel\kernel.elf"
$diskPath = Join-Path $RootDir "artifacts\disk\secureos-disk.img"
if (Test-Path $isoPath)  { Write-Host "  - artifacts\kernel\secureos.iso" }
if (Test-Path $elfPath)  { Write-Host "  - artifacts\kernel\kernel.elf" }
if (Test-Path $diskPath) { Write-Host "  - artifacts\disk\secureos-disk.img" }
