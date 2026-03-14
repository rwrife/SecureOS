[CmdletBinding()]
param(
  [ValidateSet("hello_boot", "hello_boot_fail", "kernel_console", "kernel_filedemo", "kernel_persistence")]
  [string]$Test = "hello_boot",

  [switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

function Show-Usage {
  Write-Host "Usage: run_qemu.ps1 -Test <hello_boot|hello_boot_fail|kernel_console|kernel_filedemo|kernel_persistence>"
  Write-Host ""
  Write-Host "Runs the QEMU harness inside the pinned toolchain container."
  Write-Host "Outputs:"
  Write-Host "  artifacts/qemu/<test>.log"
  Write-Host "  artifacts/qemu/<test>.meta.json"
}

if ($Help) {
  Show-Usage
  exit 0
}

$rootDir = Get-SecureOSRootDir -ScriptRoot $PSScriptRoot
$imageTag = Get-ToolchainImage
$dockerfile = Get-ToolchainDockerfile -RootDir $rootDir

Assert-DockerAvailable
Ensure-ToolchainImage -RootDir $rootDir -ImageTag $imageTag -Dockerfile $dockerfile

Invoke-ToolchainScript -RootDir $rootDir -ImageTag $imageTag -ScriptText "./build/scripts/run_qemu.sh --test $Test"
