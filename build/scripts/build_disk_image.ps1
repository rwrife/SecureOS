[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$rootDir = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$diskDir = Join-Path $rootDir "artifacts\disk"
$diskPath = Join-Path $diskDir "secureos-disk.img"
$diskBlocks = if ($env:SECUREOS_DISK_BLOCKS) { [int]$env:SECUREOS_DISK_BLOCKS } else { 4096 }

New-Item -ItemType Directory -Path $diskDir -Force | Out-Null
[System.IO.File]::WriteAllBytes($diskPath, (New-Object byte[] ($diskBlocks * 512)))
Write-Host "Built $diskPath"
Write-Host "PASS: disk image build"
