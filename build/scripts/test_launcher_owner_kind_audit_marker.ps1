#!/usr/bin/env pwsh
# build/scripts/test_launcher_owner_kind_audit_marker.ps1
#
# Windows peer for the Issue #554 owner_kind launch-audit host gate.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$rootDir = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
Set-Location $rootDir

python3 tools/check_launcher_owner_kind_audit_marker.py --root "$rootDir"
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
