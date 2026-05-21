# build/scripts/validate_manifests.ps1 — issue #195.
#
# Thin wrapper around tools/validate_manifests.py. Re-validates every
# in-tree app manifest against manifests/schema/v0.json so example +
# schema cannot silently drift (BUILD_ROADMAP §7 ABI-rot guard,
# follow-up to #187).
#
# Bash peer: build/scripts/validate_manifests.sh (kept in sync under
# the AGENTS.md cross-platform rule, #156).
#
# Exit codes mirror the Python entry point:
#   0 all manifests valid
#   1 at least one manifest failed validation
#   2 harness error (missing/unreadable schema)
$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot = (Resolve-Path (Join-Path $ScriptDir '..\..')).Path

$Python = $env:PYTHON
if ([string]::IsNullOrEmpty($Python)) {
    $Python = 'python3'
    if (-not (Get-Command $Python -ErrorAction SilentlyContinue)) {
        $Python = 'python'
    }
}

& $Python (Join-Path $RepoRoot 'tools/validate_manifests.py') @args
exit $LASTEXITCODE
