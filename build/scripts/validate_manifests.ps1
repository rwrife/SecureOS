# build/scripts/validate_manifests.ps1
#
# Issue #195: thin wrapper around tools/validate_manifests.py so the
# manifest schema check runs through the same build/scripts/* entrypoint
# convention as every other validator. Mirror script:
# build/scripts/validate_manifests.sh (AGENTS.md cross-platform parity
# rule, #156).
#
# Exit codes:
#   0 — all manifests validated against manifests/schema/v0.json
#   1 — one or more manifests failed schema validation
#   2 — environment / usage error (missing schema, missing jsonschema lib)

$ErrorActionPreference = 'Stop'

$RootDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

$Py = if ($env:PYTHON) { $env:PYTHON } else { 'python3' }
if (-not (Get-Command $Py -ErrorAction SilentlyContinue)) {
    # Fallback to Windows-launcher style.
    $Py = 'python'
    if (-not (Get-Command $Py -ErrorAction SilentlyContinue)) {
        Write-Error 'MANIFEST_VALIDATE:ERROR:python3/python not found on PATH'
        exit 2
    }
}

$Script = Join-Path $RootDir 'tools/validate_manifests.py'
& $Py $Script @args
exit $LASTEXITCODE
