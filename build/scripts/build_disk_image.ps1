[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

$rootDir = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$diskDir = Join-Path $rootDir "artifacts\disk"
$diskPath = Join-Path $diskDir "secureos-disk.img"
$diskBlocks = if ($env:SECUREOS_DISK_BLOCKS) { [int]$env:SECUREOS_DISK_BLOCKS } else { 4096 }
$imageTag = Get-ToolchainImage

Assert-DockerAvailable
Ensure-ToolchainImage -RootDir $rootDir -ImageTag $imageTag -Dockerfile (Get-ToolchainDockerfile -RootDir $rootDir)

Stop-SecureOSActiveInstances -RootDir $rootDir -ImageTag $imageTag

$previousBlocks = $env:SECUREOS_DISK_BLOCKS
$env:SECUREOS_DISK_BLOCKS = [string]$diskBlocks
try {
	Invoke-ToolchainScript -RootDir $rootDir -ImageTag $imageTag -ScriptText "./build/scripts/build_disk_image.sh"
}
finally {
	if ($null -ne $previousBlocks) {
		$env:SECUREOS_DISK_BLOCKS = $previousBlocks
	} else {
		Remove-Item Env:SECUREOS_DISK_BLOCKS -ErrorAction SilentlyContinue
	}
}

Write-Host "PASS: disk image build"
