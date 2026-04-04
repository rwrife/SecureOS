[CmdletBinding()]
param(
  [Parameter(Position = 0)]
  [string]$LibName = "envlib"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

$rootDir = Get-SecureOSRootDir -ScriptRoot $PSScriptRoot
$imageTag = Get-ToolchainImage
$dockerfile = Get-ToolchainDockerfile -RootDir $rootDir

Assert-DockerAvailable
Ensure-ToolchainImage -RootDir $rootDir -ImageTag $imageTag -Dockerfile $dockerfile

# Invoke the shell script via docker
$scriptCommand = "set -euo pipefail; ./build/scripts/build_user_lib.sh '$LibName'"
docker run --rm -v "${rootDir}:/workspace" -w /workspace $imageTag bash -lc $scriptCommand
if ($LASTEXITCODE -ne 0) {
  throw "User lib build failed with exit code $LASTEXITCODE"
}

Write-Host "PASS: user lib build ($LibName)"
