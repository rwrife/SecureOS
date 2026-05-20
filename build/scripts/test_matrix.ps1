# test_matrix.ps1 — Windows peer of test_matrix.sh.
#
# Capability matrix harness (BUILD_ROADMAP §6.1 nightly / §6.2). Iterates the
# {cap_set, faux_policy, lifecycle_event} cells declared in
# tests/matrix/capability_matrix.json and re-runs the corresponding existing
# validator targets via build/scripts/test.ps1 once per cell. Per-cell logs
# and a pass/fail JSON are written under artifacts/runs/matrix-<run-id>/,
# plus a top-level matrix_report.json summary.
#
# Kept deliberately parallel to the Bash peer (issue #151, AGENTS.md
# cross-platform rule, see also #156).
#
# Usage:
#   .\build\scripts\test_matrix.ps1
#   .\build\scripts\test_matrix.ps1 -CellIds minimal-faux_off-none
#   $env:SECUREOS_MATRIX_FILE = "path\to\matrix.json"; .\build\scripts\test_matrix.ps1

[CmdletBinding()]
param(
  [Parameter(ValueFromRemainingArguments = $true)]
  [string[]]$CellIds = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$rootDir = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$matrixFile = if ($env:SECUREOS_MATRIX_FILE) { $env:SECUREOS_MATRIX_FILE } else { Join-Path $rootDir "tests\matrix\capability_matrix.json" }

if (-not (Test-Path $matrixFile)) {
  Write-Error "test_matrix: matrix file not found: $matrixFile"
  exit 2
}

$gitSha = "unknown"
try { $gitSha = (& git -C $rootDir rev-parse HEAD 2>$null).Trim() } catch {}
$gitShort = if ($gitSha -and $gitSha -ne "unknown") { $gitSha.Substring(0, [Math]::Min(7, $gitSha.Length)) } else { "nogit" }

$runId = if ($env:SECUREOS_RUN_ID) { $env:SECUREOS_RUN_ID } else { "matrix-" + (Get-Date -AsUTC -Format "yyyyMMddTHHmmssZ") + "-" + $gitShort }
$runDir = Join-Path $rootDir "artifacts\runs\$runId"
$reportPath = Join-Path $runDir "matrix_report.json"
New-Item -ItemType Directory -Force -Path $runDir | Out-Null

$doc = Get-Content -Raw -Path $matrixFile | ConvertFrom-Json
if (-not $doc.cells -or $doc.cells.Count -eq 0) {
  Write-Error "test_matrix: no cells declared in $matrixFile"
  exit 2
}

$overallRc = 0
$cellResults = @()

foreach ($cell in $doc.cells) {
  if ($CellIds.Count -gt 0 -and ($CellIds -notcontains $cell.id)) {
    continue
  }

  $cellDir = Join-Path $runDir $cell.id
  New-Item -ItemType Directory -Force -Path $cellDir | Out-Null

  $cellConfig = [ordered]@{
    id              = $cell.id
    cap_set         = $cell.cap_set
    faux_policy     = $cell.faux_policy
    lifecycle_event = $cell.lifecycle_event
    targets         = @($cell.targets)
  }
  ($cellConfig | ConvertTo-Json -Depth 5) | Set-Content -Path (Join-Path $cellDir "cell.json") -Encoding utf8

  $cellLog = Join-Path $cellDir "cell.log"
  Set-Content -Path $cellLog -Value "" -Encoding utf8
  $cellRc = 0
  $targetStatuses = [ordered]@{}

  foreach ($target in $cell.targets) {
    $line = "::matrix:: cell=$($cell.id) target=$target"
    Add-Content -Path $cellLog -Value $line
    Write-Host $line

    $env:SECUREOS_MATRIX_CELL = $cell.id
    $env:SECUREOS_MATRIX_CAP_SET = $cell.cap_set
    $env:SECUREOS_MATRIX_FAUX_POLICY = $cell.faux_policy
    $env:SECUREOS_MATRIX_LIFECYCLE = $cell.lifecycle_event

    & (Join-Path $PSScriptRoot "test.ps1") $target *>> $cellLog
    if ($LASTEXITCODE -ne 0) {
      $cellRc = 1
      $overallRc = 1
      $targetStatuses[$target] = "fail"
    } else {
      $targetStatuses[$target] = "pass"
    }
  }

  $cellStatus = if ($cellRc -ne 0) { "fail" } else { "pass" }
  $resultObj = [ordered]@{
    cell    = $cell.id
    status  = $cellStatus
    targets = $targetStatuses
    log     = $cellLog
  }
  ($resultObj | ConvertTo-Json -Depth 5) | Set-Content -Path (Join-Path $cellDir "result.json") -Encoding utf8

  $cellResults += [ordered]@{
    id              = $cell.id
    cap_set         = $cell.cap_set
    faux_policy     = $cell.faux_policy
    lifecycle_event = $cell.lifecycle_event
    status          = $cellStatus
    targets         = $targetStatuses
    artifacts       = $cellDir
  }
}

$overallStatus = if ($overallRc -ne 0) { "fail" } else { "pass" }
$report = [ordered]@{
  schema       = "secureos.matrix_report.v0"
  run_id       = $runId
  generated_at = (Get-Date -AsUTC -Format "yyyy-MM-ddTHH:mm:ssZ")
  git_sha      = $gitSha
  matrix_file  = $matrixFile
  status       = $overallStatus
  cells        = $cellResults
}
($report | ConvertTo-Json -Depth 8) | Set-Content -Path $reportPath -Encoding utf8

Write-Host "test_matrix: report written to $reportPath (status=$overallStatus)"
exit $overallRc
