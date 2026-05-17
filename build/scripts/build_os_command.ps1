[CmdletBinding()]
param(
  [Parameter(Position = 0)]
  [string]$CommandName = "ls"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# @file build_os_command.ps1
# @brief PowerShell peer of build_os_command.sh — builds an OS command and wraps
#        it in the SecureOS File Format (SOF) container.
#
# Purpose:
#   Provide the Windows build path for OS-binary construction so contributors on
#   Windows can run the same step as Linux/macOS, per the AGENTS.md cross-platform
#   rule and the parity-drift remediation tracked in issue #156. Mirrors the
#   pattern used by build_user_app.ps1 / build_user_lib.ps1: delegate to the .sh
#   inside the pinned toolchain container so behavior is byte-identical.
#
# Usage:
#   ./build_os_command.ps1 <command_name>
#   Example: ./build_os_command.ps1 ls
#
# Output:
#   artifacts/os/<command_name>.bin (SOF-wrapped script payload)

. (Join-Path $PSScriptRoot "common.ps1")

$rootDir = Get-SecureOSRootDir -ScriptRoot $PSScriptRoot
$imageTag = Get-ToolchainImage
$dockerfile = Get-ToolchainDockerfile -RootDir $rootDir

Assert-DockerAvailable
Ensure-ToolchainImage -RootDir $rootDir -ImageTag $imageTag -Dockerfile $dockerfile

# Invoke the shell script via docker (matches build_user_app.ps1 / build_user_lib.ps1)
$scriptCommand = "set -euo pipefail; ./build/scripts/build_os_command.sh '$CommandName'"
docker run --rm -v "${rootDir}:/workspace" -w /workspace $imageTag bash -lc $scriptCommand
if ($LASTEXITCODE -ne 0) {
  throw "OS command build failed with exit code $LASTEXITCODE"
}

Write-Host "PASS: os command build ($CommandName)"
