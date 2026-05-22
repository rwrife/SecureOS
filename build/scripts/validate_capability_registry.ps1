# build/scripts/validate_capability_registry.ps1
#
# Issue #234: PowerShell peer of validate_capability_registry.sh
# (#156 cross-platform parity rule). Thin wrapper around
# tools/validate_capability_registry.py.
#
# Exit codes are passed through from the Python implementation:
#   0  registry consistent with kernel/cap/capability.h + test.sh + plans/
#   1  one or more REGISTRY_VALIDATE:FAIL markers emitted
#   2  environment / usage error (missing input file, malformed JSON)

$ErrorActionPreference = 'Stop'

$RootDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

$Py = if ($env:PYTHON) { $env:PYTHON } else { 'python3' }
if (-not (Get-Command $Py -ErrorAction SilentlyContinue)) {
    $Py = 'python'
    if (-not (Get-Command $Py -ErrorAction SilentlyContinue)) {
        Write-Error 'REGISTRY_VALIDATE:FAIL:python3_not_found'
        exit 2
    }
}

$Script = Join-Path $RootDir 'tools/validate_capability_registry.py'
& $Py $Script --root $RootDir @args
exit $LASTEXITCODE
