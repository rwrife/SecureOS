[CmdletBinding()]
param(
  [Parameter(Position = 0)]
  [string]$Target = "image",

  [switch]$Help
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

function Show-Usage {
  param(
    [string]$ImageTag
  )

  Write-Host "Usage: build.ps1 [kernel|modules|image|run|test-boot|user-app|user-lib|disk|console|graphics|build-all]"
  Write-Host ""
  Write-Host "Builds SecureOS targets using the pinned toolchain container."
  Write-Host "Environment overrides:"
  Write-Host "  SECUREOS_TOOLCHAIN_IMAGE      Container image tag (default: $ImageTag)"
  Write-Host "  SECUREOS_TOOLCHAIN_DOCKERFILE Dockerfile path for bootstrap build"
}

$imageTag = Get-ToolchainImage
if ($Help) {
  Show-Usage -ImageTag $imageTag
  exit 0
}

$rootDir = Get-SecureOSRootDir -ScriptRoot $PSScriptRoot
$dockerfile = Get-ToolchainDockerfile -RootDir $rootDir

Assert-DockerAvailable
Ensure-ToolchainImage -RootDir $rootDir -ImageTag $imageTag -Dockerfile $dockerfile

switch ($Target) {
  "kernel" {
    & (Join-Path $PSScriptRoot "build_kernel_entry.ps1")
    break
  }
  "modules" {
    Write-Host "[build] target=modules"
    Invoke-ToolchainScript -RootDir $rootDir -ImageTag $imageTag -ScriptText "echo TODO: implement modules target build graph"
    break
  }
  "image" {
    & (Join-Path $PSScriptRoot "build_kernel_image.ps1")
    break
  }
  "run" {
    & (Join-Path $PSScriptRoot "build_kernel_image.ps1")
    & (Join-Path $PSScriptRoot "run_qemu.ps1") -Test kernel_console
    break
  }
  "test-boot" {
    & (Join-Path $PSScriptRoot "test.ps1") -TestName hello_boot
    break
  }
  "user-app" {
    & (Join-Path $PSScriptRoot "build_user_app.ps1") filedemo
    break
  }
  "user-lib" {
    & (Join-Path $PSScriptRoot "build_user_lib.ps1") envlib
    break
  }
  "disk" {
    & (Join-Path $PSScriptRoot "build_disk_image.ps1")
    break
  }
  "console" {
    & (Join-Path $PSScriptRoot "boot_console.ps1")
    break
  }
  "graphics" {
    & (Join-Path $PSScriptRoot "boot_graphics.ps1")
    break
  }
  "build-all" {
    Write-Host "[build] target=build-all - building all components for testing"
    & (Join-Path $PSScriptRoot "build_bearssl.ps1")
    & (Join-Path $PSScriptRoot "build_kernel_entry.ps1")
      & (Join-Path $PSScriptRoot "build_user_lib.ps1") envlib
    & (Join-Path $PSScriptRoot "build_user_app.ps1") filedemo
    & (Join-Path $PSScriptRoot "build_kernel_image.ps1")
    & (Join-Path $PSScriptRoot "build_disk_image.ps1")
      Write-Host "[build] Completed all components for testing"
    break
  }
  default {
    Write-Host "Unknown target: $Target"
    Show-Usage -ImageTag $imageTag
    exit 1
  }
}
