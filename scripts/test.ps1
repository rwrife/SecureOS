# scripts/test.ps1 - Host-side test runner (Windows)
#
# Runs the SecureOS test suite inside the Docker toolchain container.
#
# Usage: scripts\test.ps1 [test_name|--all]  (default: --all)

[CmdletBinding()]
param(
  [Parameter(Position = 0)]
  [string]$TestName = "--all"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RootDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ImageTag = if ($env:SECUREOS_TOOLCHAIN_IMAGE) { $env:SECUREOS_TOOLCHAIN_IMAGE } else { "secureos/toolchain:bookworm-2026-02-12" }
$Dockerfile = Join-Path $RootDir "build\docker\Dockerfile.toolchain"

# Ensure Docker is available
if (-not (Get-Command docker -ErrorAction SilentlyContinue)) {
  Write-Host "ERROR: Docker is required." -ForegroundColor Red
  exit 1
}

$null = docker info 2>&1
if ($LASTEXITCODE -ne 0) {
  Write-Host "ERROR: Docker daemon is not running." -ForegroundColor Red
  exit 1
}

# Build toolchain image if needed
docker image inspect $ImageTag *> $null
if ($LASTEXITCODE -ne 0) {
  Write-Host "Building toolchain image: $ImageTag"
  docker build -f $Dockerfile -t $ImageTag $RootDir
  if ($LASTEXITCODE -ne 0) { throw "Failed to build toolchain image" }
}

# Map --all to empty arg
$TestArg = ""
if ($TestName -ne "--all") {
  $TestArg = $TestName
}

$DockerRoot = $RootDir -replace '\\', '/'

Write-Host "Running tests$(if ($TestArg) { " ($TestArg)" })..."
docker run --rm -v "${DockerRoot}:/workspace" -w /workspace $ImageTag bash -lc "set -euo pipefail; ./build/scripts/test.sh $TestArg"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
