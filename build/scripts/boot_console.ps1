[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

$rootDir = Get-SecureOSRootDir -ScriptRoot $PSScriptRoot
$imageTag = Get-ToolchainImage
$dockerfile = Get-ToolchainDockerfile -RootDir $rootDir

Assert-DockerAvailable
Ensure-ToolchainImage -RootDir $rootDir -ImageTag $imageTag -Dockerfile $dockerfile
Stop-SecureOSActiveInstances -RootDir $rootDir -ImageTag $imageTag

& (Join-Path $PSScriptRoot "build_kernel_image.ps1")
& (Join-Path $PSScriptRoot "build_disk_image.ps1")

Write-Host "Launching interactive SecureOS console..."
Write-Host "Type commands at the secureos> prompt. Use 'exit pass' to stop QEMU cleanly."

docker run --rm -it -v "${rootDir}:/workspace" -w /workspace $imageTag bash -lc "./build/scripts/run_qemu.sh --test kernel_prompt"
if ($LASTEXITCODE -eq 33) {
  Write-Host "QEMU_PASS:kernel_prompt"
  exit 0
}
if ($LASTEXITCODE -ne 0) {
  throw "Interactive kernel console session exited with code $LASTEXITCODE"
}
