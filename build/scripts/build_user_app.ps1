[CmdletBinding()]
param(
  [Parameter(Position = 0)]
  [string]$AppName = "filedemo"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

$rootDir = Get-SecureOSRootDir -ScriptRoot $PSScriptRoot
$imageTag = Get-ToolchainImage
$dockerfile = Get-ToolchainDockerfile -RootDir $rootDir

Assert-DockerAvailable
Ensure-ToolchainImage -RootDir $rootDir -ImageTag $imageTag -Dockerfile $dockerfile

$buildScript = @'
set -euo pipefail
APP_DIR="user/apps/__APP_NAME__"
test -f "$APP_DIR/main.c"
mkdir -p artifacts/user
clang --target=i386-unknown-none-elf -ffreestanding -fno-stack-protector -m32 -I user/include -c "$APP_DIR/main.c" -o "artifacts/user/__APP_NAME__.o"
clang --target=i386-unknown-none-elf -ffreestanding -fno-stack-protector -m32 -I user/include -c user/runtime/secureos_api_stubs.c -o artifacts/user/secureos_api_stubs.o
ld.lld -m elf_i386 -nostdlib -e main -o "artifacts/user/__APP_NAME__.elf" "artifacts/user/__APP_NAME__.o" artifacts/user/secureos_api_stubs.o
echo "Built artifacts/user/__APP_NAME__.elf"
'@

$buildScript = $buildScript.Replace('__APP_NAME__', $AppName)

Invoke-ToolchainScript -RootDir $rootDir -ImageTag $imageTag -ScriptText $buildScript
Write-Host "PASS: user app build ($AppName)"
