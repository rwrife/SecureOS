[CmdletBinding()]
param(
  [Parameter(Position=0)]
  [string]$Target = "image",
  [switch]$Json,
  [switch]$Help
)

# os-build.ps1 — Windows peer of build/scripts/os-build.
# Issue #162 (BUILD_ROADMAP §4.3). Thin shim that runs the .sh peer inside
# the pinned toolchain container so the JSON envelope is produced by a
# single implementation (preserves .sh ↔ .ps1 parity rule #156).

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

if ($Help) {
  Write-Host "Usage: os-build.ps1 [-Json] [-Target <name>]"
  Write-Host "Deterministic agent wrapper around build.sh; emits JSON envelope."
  exit 0
}

$rootDir = Get-SecureOSRootDir -ScriptRoot $PSScriptRoot
$imageTag = Get-ToolchainImage
$dockerfile = Get-ToolchainDockerfile -RootDir $rootDir

Assert-DockerAvailable
Ensure-ToolchainImage -RootDir $rootDir -ImageTag $imageTag -Dockerfile $dockerfile

Invoke-ToolchainScript -RootDir $rootDir -ImageTag $imageTag -ScriptText "./build/scripts/os-build $Target"
