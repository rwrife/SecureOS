[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

$rootDir = Get-SecureOSRootDir -ScriptRoot $PSScriptRoot
$imageTag = Get-ToolchainImage
$dockerfile = Get-ToolchainDockerfile -RootDir $rootDir
$argsFile = Join-Path $rootDir "build\qemu\x86_64-graphical.args"
$isoPath = Join-Path $rootDir "artifacts\kernel\secureos.iso"
$diskPath = Join-Path $rootDir "artifacts\disk\secureos-disk.img"

Assert-DockerAvailable
Ensure-ToolchainImage -RootDir $rootDir -ImageTag $imageTag -Dockerfile $dockerfile
Stop-SecureOSActiveInstances -RootDir $rootDir -ImageTag $imageTag

& (Join-Path $PSScriptRoot "build_kernel_image.ps1")
& (Join-Path $PSScriptRoot "build_disk_image.ps1")

if (-not (Test-Path $argsFile)) {
  throw "Missing QEMU args file: $argsFile"
}
if (-not (Test-Path $isoPath)) {
  throw "Missing kernel ISO: $isoPath"
}
if (-not (Test-Path $diskPath)) {
  throw "Missing disk image: $diskPath"
}

$qemuCmd = Get-Command qemu-system-x86_64 -ErrorAction SilentlyContinue
if ($null -eq $qemuCmd) {
  $qemuCmd = Get-Command qemu-system-x86_64.exe -ErrorAction SilentlyContinue
}
if ($null -eq $qemuCmd) {
  throw "qemu-system-x86_64 is required on host PATH for graphical mode. Install QEMU for Windows and retry."
}

$rawArgs = Get-Content $argsFile |
  ForEach-Object { $_.Trim() } |
  Where-Object { $_.Length -gt 0 -and -not $_.StartsWith("#") }

Write-Host "Launching SecureOS in QEMU graphical mode..."
Write-Host "Use 'exit pass' in the SecureOS console to stop cleanly."
Write-Host "Input note: type commands in this terminal (serial), not in the QEMU graphics window."

& $qemuCmd.Source `
  -cdrom $isoPath `
  -boot d `
  -drive "format=raw,file=$diskPath,if=ide,index=0,media=disk" `
  -device "isa-debug-exit,iobase=0xf4,iosize=0x04" `
  @rawArgs

if ($LASTEXITCODE -eq 33) {
  Write-Host "QEMU_PASS:kernel_prompt"
  exit 0
}
if ($LASTEXITCODE -eq 35) {
  throw "QEMU_FAIL:kernel_prompt:debug_exit=fail"
}
if ($LASTEXITCODE -ne 0) {
  throw "Graphical QEMU session exited with code $LASTEXITCODE"
}
