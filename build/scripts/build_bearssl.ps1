# build_bearssl.ps1 — Compile BearSSL objects for SecureOS freestanding x86-64.
#
# Purpose:
#   Compiles all BearSSL source files listed in Makefile.secureos into .o
#   files under artifacts/bearssl/ using the SecureOS freestanding toolchain.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

$rootDir = Get-SecureOSRootDir -ScriptRoot $PSScriptRoot
$imageTag = Get-ToolchainImage
$dockerfile = Get-ToolchainDockerfile -RootDir $rootDir

Assert-DockerAvailable
Ensure-ToolchainImage -RootDir $rootDir -ImageTag $imageTag -Dockerfile $dockerfile

# Invoke the shell script directly
docker run --rm -v "${rootDir}:/workspace" -w /workspace $imageTag bash build/scripts/build_bearssl.sh
if ($LASTEXITCODE -ne 0) {
  Write-Host "WARNING: BearSSL build failed, continuing..."
} else {
  Write-Host "PASS: BearSSL build"
}
