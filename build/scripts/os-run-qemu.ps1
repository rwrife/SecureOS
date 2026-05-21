[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)]
  [string]$Test,
  [switch]$Json,
  [switch]$Help
)

# os-run-qemu.ps1 — Windows peer of build/scripts/os-run-qemu. See #162.
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

if ($Help) {
  Write-Host "Usage: os-run-qemu.ps1 [-Json] -Test <name>"
  Write-Host "Deterministic agent wrapper around run_qemu.sh; emits JSON envelope."
  exit 0
}

$rootDir = Get-SecureOSRootDir -ScriptRoot $PSScriptRoot
$imageTag = Get-ToolchainImage
$dockerfile = Get-ToolchainDockerfile -RootDir $rootDir

Assert-DockerAvailable
Ensure-ToolchainImage -RootDir $rootDir -ImageTag $imageTag -Dockerfile $dockerfile

Invoke-ToolchainScript -RootDir $rootDir -ImageTag $imageTag -ScriptText "./build/scripts/os-run-qemu --test $Test"
