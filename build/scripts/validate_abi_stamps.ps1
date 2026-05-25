# build/scripts/validate_abi_stamps.ps1
#
# Issue #297: PowerShell peer of validate_abi_stamps.sh
# (#156 cross-platform parity rule). Thin wrapper around
# tools/validate_abi_stamps.py.
#
# Exit codes are passed through from the Python implementation:
#   0  every in-scope docs/abi/*.md stamp is at least as new as its
#      most recent content-changing commit
#   1  one or more ABI_STAMP:FAIL markers emitted (stale stamps)
#   2  environment / usage error (missing dir, not a git checkout)

$ErrorActionPreference = 'Stop'

$RootDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

$Py = if ($env:PYTHON) { $env:PYTHON } else { 'python3' }
if (-not (Get-Command $Py -ErrorAction SilentlyContinue)) {
    $Py = 'python'
    if (-not (Get-Command $Py -ErrorAction SilentlyContinue)) {
        Write-Error 'ABI_STAMP:FAIL:python3_not_found'
        exit 2
    }
}

$Script = Join-Path $RootDir 'tools/validate_abi_stamps.py'
& $Py $Script --root $RootDir @args
exit $LASTEXITCODE
