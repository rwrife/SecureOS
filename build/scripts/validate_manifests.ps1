# Validate every in-tree *.manifest.json against the canonical schema.
# Cross-platform peer: build/scripts/validate_manifests.sh (AGENTS.md / #156).
[CmdletBinding()]
param(
  [string]$Schema = $null
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$rootDir = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not $Schema) {
  if ($env:MANIFEST_SCHEMA) {
    $Schema = $env:MANIFEST_SCHEMA
  } else {
    $Schema = Join-Path $rootDir "manifests/schema/v0.json"
  }
}
$validator = Join-Path $rootDir "tools/manifest_validate/validate.py"

if (-not (Test-Path $Schema)) {
  Write-Error "validate_manifests.ps1: schema not found: $Schema"
  exit 2
}
if (-not (Test-Path $validator)) {
  Write-Error "validate_manifests.ps1: validator not found: $validator"
  exit 2
}

$manifests = Get-ChildItem -Path $rootDir -Recurse -File -Filter "*.manifest.json" |
  Where-Object {
    $rel = $_.FullName.Substring($rootDir.Length).TrimStart('\','/')
    -not ($rel.StartsWith(".git") -or $rel.StartsWith("artifacts") -or $rel.StartsWith("build/docker") -or $rel.StartsWith("build\docker"))
  } |
  Sort-Object FullName

if ($manifests.Count -eq 0) {
  Write-Error "validate_manifests.ps1: no *.manifest.json found under $rootDir"
  exit 2
}

Write-Host "validate_manifests.ps1: schema=$Schema, $($manifests.Count) manifest(s)"
$argsList = @("--schema", $Schema) + ($manifests | ForEach-Object { $_.FullName })
& python3 $validator @argsList
exit $LASTEXITCODE
