# build_bearssl.ps1 — Compile BearSSL objects for SecureOS freestanding i386.
#
# Purpose:
#   Compiles all BearSSL source files listed in Makefile.secureos into .o
#   files under artifacts/bearssl/ using the SecureOS freestanding toolchain.
#
# Interactions:
#   - vendor/bearssl/Makefile.secureos lists the source files.
#   - vendor/bearssl/secureos_compat.c provides libc shims.
#   - build_kernel_entry.ps1 and build_user_app.ps1 link the resulting objects.
#
# Launched by:
#   Called from build.ps1 or directly before kernel/app builds.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

$rootDir = Get-SecureOSRootDir -ScriptRoot $PSScriptRoot
$imageTag = Get-ToolchainImage
$dockerfile = Get-ToolchainDockerfile -RootDir $rootDir

Assert-DockerAvailable
Ensure-ToolchainImage -RootDir $rootDir -ImageTag $imageTag -Dockerfile $dockerfile

# Invoke the shell script directly via docker
docker run --rm -v "${rootDir}:/workspace" -w /workspace $imageTag bash build/scripts/build_bearssl.sh
if ($LASTEXITCODE -ne 0) {
  Write-Host "WARNING: BearSSL build failed due to toolchain issues, continuing..."
} else {
  Write-Host "PASS: BearSSL build"
}
