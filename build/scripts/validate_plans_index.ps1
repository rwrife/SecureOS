# build/scripts/validate_plans_index.ps1
#
# Thin wrapper around tools/validate_plans_index.py so plans index validation
# follows the standard build/scripts entrypoint pattern.
# Mirror script: build/scripts/validate_plans_index.sh.
#
# Exit codes:
#   0 — all plans are indexed exactly once
#   1 — one or more plans are missing / duplicated / stale-indexed
#   2 — usage / environment error

$ErrorActionPreference = 'Stop'

$RootDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

$Py = if ($env:PYTHON) { $env:PYTHON } else { 'python3' }
if (-not (Get-Command $Py -ErrorAction SilentlyContinue)) {
    $Py = 'python'
    if (-not (Get-Command $Py -ErrorAction SilentlyContinue)) {
        Write-Error 'PLANS_INDEX:ERROR:python3/python not found on PATH'
        exit 2
    }
}

$Script = Join-Path $RootDir 'tools/validate_plans_index.py'
& $Py $Script --root $RootDir @args
exit $LASTEXITCODE
