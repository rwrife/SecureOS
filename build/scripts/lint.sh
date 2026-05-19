#!/usr/bin/env bash
# build/scripts/lint.sh — BUILD_ROADMAP §6.1 stage 1 (lint / format / static).
#
# Runs the cheap, host-local pre-PR checks:
#   1. clang-format --dry-run --Werror across the canonical C dirs
#   2. shellcheck across build/scripts/*.sh
#   3. (optional) .sh ↔ .ps1 parity, if the parity check is present (#156)
#
# Emits the standard TEST:START / TEST:PASS / TEST:FAIL markers so this
# step can be consumed by the same log parsers as the rest of the
# validator bundle.
#
# Issue: #182. Companion of build/scripts/lint.ps1 (same contract,
# Windows host).
#
# Exit codes:
#   0  all checks passed (or were cleanly skipped because the tool was
#      missing AND `LINT_REQUIRE_TOOLS=0`, the default)
#   1  at least one check failed
#   2  required tool was missing and `LINT_REQUIRE_TOOLS=1`
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
cd "$REPO_ROOT" || exit 1

# Dirs in scope for clang-format. sdk/ is intentionally listed even though
# it does not exist yet (#136); the glob expansion no-ops cleanly.
CLANG_FORMAT_DIRS=(kernel user tools tests sdk)
CLANG_FORMAT_EXTS=(c h cpp hpp)

# shellcheck disable=SC2034  # consumed by the parity check via env
LINT_REQUIRE_TOOLS="${LINT_REQUIRE_TOOLS:-0}"

# When non-empty, treat clang-format diffs as failures. Default off so
# that landing the lint stage on a not-yet-fully-formatted tree is
# visible-but-non-blocking, matching the #174 / #176 (determinism check)
# rollout pattern. CI workflow flips this on for shellcheck + parity but
# leaves clang-format informational until a tree-wide reformat lands.
LINT_CLANG_FORMAT_FATAL="${LINT_CLANG_FORMAT_FATAL:-0}"

fail_count=0
miss_count=0

emit_marker() { printf '%s\n' "$1"; }

run_clang_format() {
    emit_marker "TEST:START:lint_clang_format"
    if ! command -v clang-format >/dev/null 2>&1; then
        if [ "$LINT_REQUIRE_TOOLS" = "1" ]; then
            emit_marker "TEST:FAIL:lint_clang_format:tool_missing"
            miss_count=$((miss_count + 1))
            return
        fi
        emit_marker "TEST:PASS:lint_clang_format:tool_missing_skipped"
        return
    fi

    local files=()
    local d ext
    for d in "${CLANG_FORMAT_DIRS[@]}"; do
        [ -d "$d" ] || continue
        for ext in "${CLANG_FORMAT_EXTS[@]}"; do
            while IFS= read -r -d '' f; do
                files+=("$f")
            done < <(find "$d" -type f -name "*.$ext" -print0)
        done
    done

    if [ "${#files[@]}" -eq 0 ]; then
        emit_marker "TEST:PASS:lint_clang_format:no_files"
        return
    fi

    local rc=0
    if ! clang-format --dry-run --Werror "${files[@]}" 2>&1; then
        rc=1
    fi

    if [ "$rc" -ne 0 ]; then
        if [ "$LINT_CLANG_FORMAT_FATAL" = "1" ]; then
            emit_marker "TEST:FAIL:lint_clang_format:diff"
            fail_count=$((fail_count + 1))
        else
            # Visible but non-blocking — see header comment.
            emit_marker "TEST:PASS:lint_clang_format:diff_informational"
        fi
    else
        emit_marker "TEST:PASS:lint_clang_format:${#files[@]}_files_clean"
    fi
}

run_shellcheck() {
    emit_marker "TEST:START:lint_shellcheck"
    if ! command -v shellcheck >/dev/null 2>&1; then
        if [ "$LINT_REQUIRE_TOOLS" = "1" ]; then
            emit_marker "TEST:FAIL:lint_shellcheck:tool_missing"
            miss_count=$((miss_count + 1))
            return
        fi
        emit_marker "TEST:PASS:lint_shellcheck:tool_missing_skipped"
        return
    fi

    local files=()
    while IFS= read -r -d '' f; do
        files+=("$f")
    done < <(find build/scripts -maxdepth 1 -type f -name "*.sh" -print0)

    if [ "${#files[@]}" -eq 0 ]; then
        emit_marker "TEST:PASS:lint_shellcheck:no_files"
        return
    fi

    # -S warning: don't drown the first run in style nits, but still
    #   surface real bugs (SC2086 unquoted expansion, SC2046, etc).
    # -e SC1091: shellcheck cannot follow sourced paths in CI without
    #   the full repo on its search path; rely on `bash -n` for that.
    if shellcheck -S warning -e SC1091 "${files[@]}"; then
        emit_marker "TEST:PASS:lint_shellcheck:${#files[@]}_files_clean"
    else
        emit_marker "TEST:FAIL:lint_shellcheck:warnings"
        fail_count=$((fail_count + 1))
    fi
}

run_parity() {
    emit_marker "TEST:START:lint_parity"
    # #156 landed an explicit parity check; prefer it when present so
    # there's exactly one implementation of the rule.
    if [ -x build/scripts/check_shell_parity.sh ]; then
        if build/scripts/check_shell_parity.sh; then
            emit_marker "TEST:PASS:lint_parity:check_shell_parity"
        else
            emit_marker "TEST:FAIL:lint_parity:check_shell_parity"
            fail_count=$((fail_count + 1))
        fi
        return
    fi

    # Fallback: inline minimal parity sketch. Intentionally permissive —
    # do not duplicate the #156 allowlist here.
    local sh ps1 base
    local sh_only=0
    for sh in build/scripts/*.sh; do
        base="$(basename "$sh" .sh)"
        ps1="build/scripts/${base}.ps1"
        if [ ! -f "$ps1" ]; then
            sh_only=$((sh_only + 1))
        fi
    done
    # The fallback is informational only; defer hard enforcement to #156.
    emit_marker "TEST:PASS:lint_parity:fallback_sh_only=${sh_only}"
}

run_clang_format
run_shellcheck
run_parity

if [ "$miss_count" -gt 0 ]; then
    emit_marker "TEST:FAIL:lint:tools_missing=${miss_count}"
    exit 2
fi
if [ "$fail_count" -gt 0 ]; then
    emit_marker "TEST:FAIL:lint:failed_checks=${fail_count}"
    exit 1
fi
emit_marker "TEST:PASS:lint"
exit 0
