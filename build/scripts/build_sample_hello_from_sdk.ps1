# build_sample_hello_from_sdk.ps1
# M6-SDK-004 starter (issue #584): Windows peer for build_sample_hello_from_sdk.sh.

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$RootDir = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$Script = Join-Path $RootDir "build/scripts/build_sample_hello_from_sdk.sh"

if (-not (Test-Path -LiteralPath $Script)) {
  Write-Error "BUILD_SAMPLE_HELLO_FROM_SDK:FAIL:missing_script:$Script"
}

bash $Script
if ($LASTEXITCODE -ne 0) {
  exit $LASTEXITCODE
}
