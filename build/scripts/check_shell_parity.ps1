# check_shell_parity.ps1 — enforce AGENTS.md "Keep ps1 and sh scripts in sync"
# (root AGENTS.md, "Guidlines" section).
#
# PowerShell mirror of check_shell_parity.sh. Walks build/scripts/ and reports
# any *.sh without a sibling *.ps1 (and vice versa). Intentional asymmetries
# live in build/scripts/.shell_parity_allowlist (one bare name per line,
# comments with '#'). Exits non-zero on unexplained drift so CI can wire this
# in.
#
# Usage:
#   pwsh build/scripts/check_shell_parity.ps1                 # check build/scripts/
#   pwsh build/scripts/check_shell_parity.ps1 path\to\dir ... # check given dirs
#
# Tracked by SecureOS issue #156.

[CmdletBinding()]
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Dirs
)

$ErrorActionPreference = 'Stop'

$RootDir = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$AllowlistFile = Join-Path $RootDir 'build/scripts/.shell_parity_allowlist'

if (-not $Dirs -or $Dirs.Count -eq 0) {
    $Dirs = @((Join-Path $RootDir 'build/scripts'))
}

$allow = @{}
if (Test-Path $AllowlistFile) {
    foreach ($line in Get-Content -LiteralPath $AllowlistFile) {
        $stripped = ($line -replace '#.*$', '').Trim()
        if ([string]::IsNullOrEmpty($stripped)) { continue }
        $allow[$stripped] = $true
    }
}

$missingPs1 = New-Object System.Collections.Generic.List[string]
$missingSh  = New-Object System.Collections.Generic.List[string]

foreach ($dir in $Dirs) {
    if (-not (Test-Path -LiteralPath $dir -PathType Container)) {
        Write-Error "check_shell_parity: not a directory: $dir"
        exit 2
    }

    Get-ChildItem -LiteralPath $dir -Filter '*.sh' -File | ForEach-Object {
        $base = [System.IO.Path]::GetFileNameWithoutExtension($_.Name)
        if ($allow.ContainsKey($base)) { return }
        $peer = Join-Path $dir ($base + '.ps1')
        if (-not (Test-Path -LiteralPath $peer -PathType Leaf)) {
            $missingPs1.Add($_.FullName) | Out-Null
        }
    }

    Get-ChildItem -LiteralPath $dir -Filter '*.ps1' -File | ForEach-Object {
        $base = [System.IO.Path]::GetFileNameWithoutExtension($_.Name)
        if ($allow.ContainsKey($base)) { return }
        $peer = Join-Path $dir ($base + '.sh')
        if (-not (Test-Path -LiteralPath $peer -PathType Leaf)) {
            $missingSh.Add($_.FullName) | Out-Null
        }
    }
}

$status = 0

if ($missingPs1.Count -gt 0) {
    $status = 1
    Write-Error "PARITY:MISSING_PS1: $($missingPs1.Count) .sh script(s) have no .ps1 peer"
    foreach ($f in $missingPs1) { Write-Error "  - $f" }
}

if ($missingSh.Count -gt 0) {
    $status = 1
    Write-Error "PARITY:MISSING_SH: $($missingSh.Count) .ps1 script(s) have no .sh peer"
    foreach ($f in $missingSh) { Write-Error "  - $f" }
}

if ($status -eq 0) {
    Write-Host "PARITY:OK: build/scripts/ .sh <-> .ps1 in sync (allowlist: $($allow.Count) entries)"
} else {
    Write-Error @"

AGENTS.md requires .sh and .ps1 build scripts to stay in sync.
To resolve, either:
  - port the missing script to the other shell, OR
  - add its bare name (no extension) to build/scripts/.shell_parity_allowlist
    with a comment explaining why the asymmetry is intentional.

Tracked by SecureOS issue #156.
"@
}

exit $status
