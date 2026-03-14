[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

$rootDir = Get-SecureOSRootDir -ScriptRoot $PSScriptRoot
$imageTag = Get-ToolchainImage
$dockerfile = Get-ToolchainDockerfile -RootDir $rootDir

& (Join-Path $PSScriptRoot "build_kernel_entry.ps1")

Assert-DockerAvailable
Ensure-ToolchainImage -RootDir $rootDir -ImageTag $imageTag -Dockerfile $dockerfile

$buildScript = @'
set -euo pipefail
test -f artifacts/kernel/kernel.elf
grub-file --is-x86-multiboot artifacts/kernel/kernel.elf
rm -rf artifacts/iso
mkdir -p artifacts/iso/boot/grub
cp build/grub/grub.cfg artifacts/iso/boot/grub/grub.cfg
cp artifacts/kernel/kernel.elf artifacts/iso/boot/kernel.elf
grub-mkrescue -o artifacts/kernel/secureos.iso artifacts/iso >/dev/null 2>&1
echo "Built artifacts/kernel/secureos.iso"
'@

Invoke-ToolchainScript -RootDir $rootDir -ImageTag $imageTag -ScriptText $buildScript
Write-Host "PASS: kernel ISO build"
