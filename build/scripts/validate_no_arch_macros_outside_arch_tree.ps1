# build/scripts/validate_no_arch_macros_outside_arch_tree.ps1
#
# Windows peer for the multi-arch portability drift gate (issue #623).
# Runs the same Python implementation used by the .sh wrapper.

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RootDir = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$Validator = Join-Path $RootDir "tools\validate_no_arch_macros_outside_arch_tree.py"
$Allowlist = Join-Path $RootDir "build\scripts\.arch_macro_allowlist"

$pythonCmd = $env:PYTHON
if ([string]::IsNullOrWhiteSpace($pythonCmd)) {
  if (Get-Command python3 -ErrorAction SilentlyContinue) {
    $pythonCmd = "python3"
  } elseif (Get-Command python -ErrorAction SilentlyContinue) {
    $pythonCmd = "python"
  } else {
    Write-Error "ARCH_MACRO_VALIDATE:FAIL:python_not_found"
    exit 2
  }
}

& $pythonCmd $Validator --root $RootDir --allowlist $Allowlist @args
exit $LASTEXITCODE
