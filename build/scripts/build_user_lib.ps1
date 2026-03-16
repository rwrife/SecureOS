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

$buildScript = @'
set -euo pipefail
LIB_DIR="user/libs/__LIB_NAME__"
test -f "$LIB_DIR/main.c"
mkdir -p artifacts/lib
clang --target=i386-unknown-none-elf -ffreestanding -fno-stack-protector -m32 -I user/include -c "$LIB_DIR/main.c" -o "artifacts/lib/__LIB_NAME__.o"
clang --target=i386-unknown-none-elf -ffreestanding -fno-stack-protector -m32 -I user/include -c user/runtime/secureos_api_stubs.c -o artifacts/lib/secureos_api_stubs.o
ld.lld -m elf_i386 -nostdlib -e main -o "artifacts/lib/__LIB_NAME__.elf" "artifacts/lib/__LIB_NAME__.o" artifacts/lib/secureos_api_stubs.o
if [ ! -f "tools/sof_wrap/sof_wrap" ]; then make -C tools/sof_wrap; fi
./tools/sof_wrap/sof_wrap --type lib --name "__LIB_NAME__" --author "SecureOS" --version "1.0.0" --date "$(date -u +%Y-%m-%d)" "artifacts/lib/__LIB_NAME__.elf" "artifacts/lib/__LIB_NAME__.lib"
echo "Built artifacts/lib/__LIB_NAME__.lib"
'@

$buildScript = $buildScript.Replace('__LIB_NAME__', $LibName)

Invoke-ToolchainScript -RootDir $rootDir -ImageTag $imageTag -ScriptText $buildScript
Write-Host "PASS: user lib build ($LibName)"
