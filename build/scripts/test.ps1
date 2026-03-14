[CmdletBinding()]
param(
  [Parameter(Position = 0)]
  [string]$TestName = "hello_boot",

  [switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

function Show-Usage {
  Write-Host "Usage: test.ps1 [hello_boot|hello_boot_negative|cap_api_contract|capability_table|capability_gate|capability_audit|event_bus|fs_service|app_runtime|kernel_console|kernel_filedemo|kernel_persistence]"
  Write-Host ""
  Write-Host "Runs SecureOS test targets inside the pinned toolchain container."
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

$testScript = switch ($TestName) {
  "hello_boot" { "./build/scripts/test_boot_sector.sh; ./build/scripts/run_qemu.sh --test hello_boot" }
  "hello_boot_negative" { "./build/scripts/test_boot_sector_fail.sh; ./build/scripts/run_qemu.sh --test hello_boot_fail" }
  "cap_api_contract" { "./build/scripts/test_cap_api_contract.sh" }
  "capability_table" { "./build/scripts/test_capability_table.sh" }
  "capability_gate" { "./build/scripts/test_capability_gate.sh" }
  "capability_audit" { "./build/scripts/test_capability_audit.sh" }
  "event_bus" { "./build/scripts/test_event_bus.sh" }
  "fs_service" { "./build/scripts/test_fs_service.sh" }
  "app_runtime" { "./build/scripts/test_app_runtime.sh" }
  "kernel_console" { "./build/scripts/build_kernel_image.sh; ./build/scripts/build_disk_image.sh; ./build/scripts/run_qemu.sh --test kernel_console" }
  "kernel_filedemo" { "./build/scripts/build_kernel_image.sh; ./build/scripts/build_disk_image.sh; ./build/scripts/run_qemu.sh --test kernel_filedemo" }
  "kernel_persistence" { "./build/scripts/test_kernel_persistence.sh" }
  default {
    Write-Host "Unknown test: $TestName"
    Show-Usage
    exit 1
  }
}

Invoke-ToolchainScript -RootDir $rootDir -ImageTag $imageTag -ScriptText $testScript
