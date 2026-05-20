[CmdletBinding()]
param(
  [switch]$Json,
  [switch]$Help
)

# os-snapshot.ps1 — Windows peer of build/scripts/os-snapshot. See #162.
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

if ($Help) {
  Write-Host "Usage: os-snapshot.ps1 [-Json]"
  Write-Host "Tars artifacts/runs/<run_id>/ into artifacts/snapshots/; emits JSON envelope."
  exit 0
}

$rootDir = Get-SecureOSRootDir -ScriptRoot $PSScriptRoot
$imageTag = Get-ToolchainImage
$dockerfile = Get-ToolchainDockerfile -RootDir $rootDir

Assert-DockerAvailable
Ensure-ToolchainImage -RootDir $rootDir -ImageTag $imageTag -Dockerfile $dockerfile

Invoke-ToolchainScript -RootDir $rootDir -ImageTag $imageTag -ScriptText "./build/scripts/os-snapshot"
