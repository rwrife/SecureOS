# build/scripts/lint.ps1 — Windows peer of lint.sh (BUILD_ROADMAP §6.1
# stage 1, issue #182).
#
# Follows the established cross-platform pattern (#156): delegate to the
# .sh implementation inside the pinned toolchain container so there is
# exactly one source of truth. The container does not currently ship
# clang-format / shellcheck (see build/docker/Dockerfile.toolchain), so
# the lint script's `LINT_REQUIRE_TOOLS=0` default kicks in and reports
# tool-missing as PASS:tool_missing_skipped rather than FAIL — matching
# the lint.sh contract.

$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot  = (Resolve-Path (Join-Path $scriptDir "..\..")).Path
$image     = "secureos/toolchain:bookworm-2026-02-12"

# Forward LINT_* env vars so Windows callers can tighten enforcement
# without editing this wrapper.
$envArgs = @()
foreach ($name in @("LINT_REQUIRE_TOOLS", "LINT_CLANG_FORMAT_FATAL")) {
    $val = [System.Environment]::GetEnvironmentVariable($name)
    if ($null -ne $val -and $val -ne "") {
        $envArgs += @("-e", "$name=$val")
    }
}

docker run --rm `
    -v "${repoRoot}:/workspace" `
    -w /workspace `
    @envArgs `
    $image `
    bash build/scripts/lint.sh

exit $LASTEXITCODE
