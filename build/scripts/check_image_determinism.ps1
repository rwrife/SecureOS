[CmdletBinding()]
param(
  [ValidateSet("disk", "image", "both")]
  [string]$Target = "disk",

  [switch]$Help
)

# PowerShell parity wrapper for check_image_determinism.sh.
# Delegates to the .sh implementation inside the pinned toolchain container,
# matching the cross-platform pattern from AGENTS.md (one implementation,
# both hosts).

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "common.ps1")

if ($Help) {
  Write-Host "Usage: check_image_determinism.ps1 -Target <disk|image|both>"
  Write-Host ""
  Write-Host "Builds the named target twice from a clean tree and asserts the"
  Write-Host "two sha256 sums match. Writes the result into the per-run"
  Write-Host "artifact bundle at artifacts/runs/<id>/ (see #161)."
  exit 0
}

$rootDir = Get-SecureOSRootDir -ScriptRoot $PSScriptRoot
$imageTag = Get-ToolchainImage

Push-Location $rootDir
try {
  $env:SECUREOS_DET_TARGET = $Target
  docker run --rm `
    -v "${rootDir}:/workspace" `
    -w /workspace `
    -e "SECUREOS_DET_TARGET=$Target" `
    -e "SECUREOS_RUN_ID=$($env:SECUREOS_RUN_ID)" `
    $imageTag `
    bash -lc './build/scripts/check_image_determinism.sh'
  exit $LASTEXITCODE
} finally {
  Pop-Location
}
