# test_shell_parity.ps1 — validator wrapper around check_shell_parity.ps1
# that emits TEST:START / TEST:PASS / TEST:FAIL lines consumable by
# test.ps1. Tracked by SecureOS issue #156.

[CmdletBinding()]
param()

$ErrorActionPreference = 'Continue'

$Target = 'parity'
$RootDir = Resolve-Path (Join-Path $PSScriptRoot '..\..')

Write-Host "TEST:START:$Target"

& pwsh -NoProfile -File (Join-Path $RootDir 'build/scripts/check_shell_parity.ps1')
$rc = $LASTEXITCODE

if ($rc -eq 0) {
    Write-Host "TEST:PASS:${Target}:build_scripts_sh_ps1_in_sync"
    exit 0
} else {
    Write-Host "TEST:FAIL:${Target}:shell_parity_drift_detected"
    exit 1
}
