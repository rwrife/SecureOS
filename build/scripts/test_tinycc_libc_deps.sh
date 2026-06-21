#!/usr/bin/env bash
# test_tinycc_libc_deps.sh — Host drift gate for the freestanding TinyCC
# libc symbol surface (issue #408 Phase 2 sub-slice, M7-TOOLCHAIN-005).
#
# Pins porting note 2 of vendor/tinycc/Makefile.secureos: "Satisfy libtcc's
# libc dependencies from user/libs/clib". This gate enumerates the libc
# symbols the pinned source set (TCC_ALL_SRCS, pinned by tinycc_vendor_gate
# / PR #516) actually calls, partitions them into clib-provided vs.
# not-yet-provided, and asserts the partition matches the audited snapshot
# stored in vendor/tinycc/libc-deps.json.
#
# Companion to:
#   - tinycc_vendor_gate         (PR #516) — pins the SOURCE set.
#   - tinycc_config_secureos     (PR #519) — pins the CONFIG header.
#   - tinycc_libc_deps           (this gate, issue #408) — pins the LIBC surface.
#
# Together they keep all three phase-3 inputs auditable while the actual
# freestanding build wiring is in flight. Same scaffold-first shape as
# bearssl_compile / tinycc_vendor_gate.
#
# Sub-markers:
#   tinycc_libc_deps_manifest_present
#       vendor/tinycc/libc-deps.json exists at the expected path.
#
#   tinycc_libc_deps_manifest_parses
#       The manifest is valid JSON and exposes the required top-level keys.
#
#   tinycc_libc_deps_sources_present
#       Every TU listed in `scannedSources` exists under
#       vendor/tinycc/tinycc/ (cross-checks vendor/tinycc/Makefile.secureos
#       TCC_ALL_SRCS — same nine TUs).
#
#   tinycc_libc_deps_scan_matches_manifest
#       Running the scanner across `scannedSources` for the `scanScope`
#       symbol set yields exactly the `required` symbol set in the
#       manifest, and the per-symbol TU list matches.
#
#   tinycc_libc_deps_provided_set_matches
#       For every `required` entry with a non-null `clib_provider`, the
#       named header file exists AND declares the symbol as a function
#       prototype. The aggregate "provided" count matches summary.providedByClib.
#
#   tinycc_libc_deps_missing_set_matches
#       For every `required` entry with a null `clib_provider`, NO clib
#       header under user/libs/clib/include/clib/ declares the symbol as
#       a function prototype. The aggregate "missing" count matches
#       summary.notYetProvided.
#
#   tinycc_libc_deps
#       Rollup: all of the above PASSed.
#
# SKIPs cleanly when:
#   - python3 is not on PATH (mirrors validate_capability_registry's
#     "no python → SKIP" arm).
#   - The tinycc submodule has not been initialized (no vendor/tinycc/tinycc/
#     contents) — mirrors tinycc_vendor_gate's submodule-not-inited SKIP.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
MANIFEST_REL="vendor/tinycc/libc-deps.json"
MANIFEST_ABS="$ROOT_DIR/$MANIFEST_REL"
VENDOR_SRC_DIR="$ROOT_DIR/vendor/tinycc/tinycc"
CLIB_HDR_DIR="$ROOT_DIR/user/libs/clib/include/clib"

emit() { printf '%s\n' "$1"; }
pass() { emit "TEST:PASS:$1"; }
skip() { emit "TEST:SKIP:$1:$2"; }
fail() { emit "TEST:FAIL:$1:$2"; ALL_OK=0; }

ALL_OK=1

# --- 1. manifest_present -----------------------------------------------------
if [ -f "$MANIFEST_ABS" ]; then
  pass "tinycc_libc_deps_manifest_present"
else
  fail "tinycc_libc_deps_manifest_present" "missing:$MANIFEST_REL"
  emit "TEST:FAIL:tinycc_libc_deps:manifest_missing"
  exit 1
fi

# --- python3 dependency ------------------------------------------------------
if ! command -v python3 >/dev/null 2>&1; then
  skip "tinycc_libc_deps_manifest_parses"           "no_python3_on_path"
  skip "tinycc_libc_deps_sources_present"           "no_python3_on_path"
  skip "tinycc_libc_deps_scan_matches_manifest"     "no_python3_on_path"
  skip "tinycc_libc_deps_provided_set_matches"      "no_python3_on_path"
  skip "tinycc_libc_deps_missing_set_matches"       "no_python3_on_path"
  pass "tinycc_libc_deps"
  exit 0
fi

# --- 2. manifest_parses ------------------------------------------------------
if python3 - "$MANIFEST_ABS" <<'PY' >/tmp/tinycc_libc_deps.stderr 2>&1
import json, sys
d = json.load(open(sys.argv[1]))
for k in ("schemaVersion","issue","scannedSources","scanScope","required","summary"):
    assert k in d, f"missing top-level key: {k}"
assert d["schemaVersion"] == 1
assert d["issue"] == 408
assert isinstance(d["scannedSources"], list) and d["scannedSources"]
assert isinstance(d["scanScope"].get("symbols"), list)
assert isinstance(d["required"], dict)
for k in ("totalRequired","providedByClib","notYetProvided"):
    assert k in d["summary"]
PY
then
  pass "tinycc_libc_deps_manifest_parses"
else
  fail "tinycc_libc_deps_manifest_parses" "$(tr '\n' ';' </tmp/tinycc_libc_deps.stderr | cut -c1-200)"
  emit "TEST:FAIL:tinycc_libc_deps:manifest_parse_error"
  exit 1
fi

# --- 3. sources_present (submodule check + per-TU existence) -----------------
# SKIP cleanly when the submodule is not initialized (mirrors tinycc_vendor_gate).
if [ ! -d "$VENDOR_SRC_DIR" ] || [ -z "$(ls -A "$VENDOR_SRC_DIR" 2>/dev/null)" ]; then
  skip "tinycc_libc_deps_sources_present"           "tinycc_submodule_not_initialized"
  skip "tinycc_libc_deps_scan_matches_manifest"     "tinycc_submodule_not_initialized"
  skip "tinycc_libc_deps_provided_set_matches"      "tinycc_submodule_not_initialized"
  skip "tinycc_libc_deps_missing_set_matches"       "tinycc_submodule_not_initialized"
  pass "tinycc_libc_deps"
  exit 0
fi

if python3 - "$MANIFEST_ABS" "$VENDOR_SRC_DIR" <<'PY' >/tmp/tinycc_libc_deps.stderr 2>&1
import json, os, sys
m = json.load(open(sys.argv[1]))
src_dir = sys.argv[2]
missing = [s for s in m["scannedSources"] if not os.path.isfile(os.path.join(src_dir, s))]
if missing:
    print("missing_sources:" + ",".join(missing))
    sys.exit(1)
PY
then
  pass "tinycc_libc_deps_sources_present"
else
  fail "tinycc_libc_deps_sources_present" "$(cat /tmp/tinycc_libc_deps.stderr | tr '\n' ';' | cut -c1-200)"
fi

# --- 4. scan_matches_manifest ------------------------------------------------
# Re-run the scanner and compare to the manifest. Mismatches fail with the
# offending symbol(s) named so a TinyCC bump that grows/shrinks the libc
# surface is caught immediately.
if python3 - "$MANIFEST_ABS" "$VENDOR_SRC_DIR" <<'PY' >/tmp/tinycc_libc_deps.stderr 2>&1
import json, os, re, sys
m = json.load(open(sys.argv[1]))
src_dir = sys.argv[2]
scope = set(m["scanScope"]["symbols"])
sources = m["scannedSources"]
hits = {}
for s in sources:
    p = os.path.join(src_dir, s)
    if not os.path.isfile(p):
        continue
    txt = open(p, "r", errors="replace").read()
    txt = re.sub(r"/\*.*?\*/", "", txt, flags=re.S)
    txt = re.sub(r"//[^\n]*", "", txt)
    txt = re.sub(r'"(?:\\.|[^"\\])*"', '""', txt)
    for mm in re.finditer(r"\b([A-Za-z_][A-Za-z_0-9]*)\s*\(", txt):
        n = mm.group(1)
        if n in scope:
            hits.setdefault(n, set()).add(s)
live = {k: sorted(v) for k, v in hits.items()}
pinned = {k: list(v["tus"]) for k, v in m["required"].items()}

extra   = sorted(set(live) - set(pinned))
absent  = sorted(set(pinned) - set(live))
if extra:
    print("symbols_in_source_not_in_manifest:" + ",".join(extra)); sys.exit(1)
if absent:
    print("symbols_in_manifest_not_in_source:" + ",".join(absent)); sys.exit(1)
for k in sorted(live):
    if sorted(live[k]) != sorted(pinned[k]):
        print(f"tu_list_mismatch:{k}:live={','.join(sorted(live[k]))}:pinned={','.join(sorted(pinned[k]))}")
        sys.exit(1)
total = len(live)
if total != m["summary"]["totalRequired"]:
    print(f"total_count_mismatch:live={total}:pinned={m['summary']['totalRequired']}")
    sys.exit(1)
PY
then
  pass "tinycc_libc_deps_scan_matches_manifest"
else
  fail "tinycc_libc_deps_scan_matches_manifest" "$(cat /tmp/tinycc_libc_deps.stderr | tr '\n' ';' | cut -c1-200)"
fi

# --- 5. provided_set_matches -------------------------------------------------
# For each symbol with a non-null clib_provider, the header file exists and
# declares the symbol as a function (matches `name(`). Aggregate count must
# equal summary.providedByClib.
if python3 - "$MANIFEST_ABS" "$ROOT_DIR" <<'PY' >/tmp/tinycc_libc_deps.stderr 2>&1
import json, os, re, sys
m = json.load(open(sys.argv[1]))
root = sys.argv[2]
provided_entries = [(s, e["clib_provider"])
                    for s, e in m["required"].items()
                    if e["clib_provider"] is not None]
errors = []
for sym, hdr in provided_entries:
    p = os.path.join(root, hdr)
    if not os.path.isfile(p):
        errors.append(f"provider_header_missing:{sym}:{hdr}")
        continue
    txt = open(p, "r", errors="replace").read()
    txt = re.sub(r"/\*.*?\*/", "", txt, flags=re.S)
    txt = re.sub(r"//[^\n]*", "", txt)
    if not re.search(r"\b" + re.escape(sym) + r"\s*\(", txt):
        errors.append(f"symbol_not_declared:{sym}:in:{hdr}")
if errors:
    print(";".join(errors)); sys.exit(1)
count = len(provided_entries)
if count != m["summary"]["providedByClib"]:
    print(f"provided_count_mismatch:live={count}:pinned={m['summary']['providedByClib']}")
    sys.exit(1)
PY
then
  pass "tinycc_libc_deps_provided_set_matches"
else
  fail "tinycc_libc_deps_provided_set_matches" "$(cat /tmp/tinycc_libc_deps.stderr | tr '\n' ';' | cut -c1-200)"
fi

# --- 6. missing_set_matches --------------------------------------------------
# For each symbol with a null clib_provider, NO clib header declares it as a
# function prototype. Aggregate count must equal summary.notYetProvided.
# (Catches the case where Phase 3 adds, say, `realloc` to clib but forgets to
# repoint libc-deps.json from null → the new header.)
if python3 - "$MANIFEST_ABS" "$CLIB_HDR_DIR" <<'PY' >/tmp/tinycc_libc_deps.stderr 2>&1
import json, os, re, sys, glob
m = json.load(open(sys.argv[1]))
hdr_dir = sys.argv[2]
missing_entries = [s for s, e in m["required"].items()
                   if e["clib_provider"] is None]
all_hdr_text = ""
for h in sorted(glob.glob(os.path.join(hdr_dir, "*.h"))):
    txt = open(h, "r", errors="replace").read()
    txt = re.sub(r"/\*.*?\*/", "", txt, flags=re.S)
    txt = re.sub(r"//[^\n]*", "", txt)
    all_hdr_text += "\n" + txt
errors = []
for sym in missing_entries:
    # Match `name(` not preceded by an alphanumeric (so `clib_free(` doesn't
    # match `free`). Allow `*` and whitespace before.
    if re.search(r"(?<![A-Za-z0-9_])" + re.escape(sym) + r"\s*\(", all_hdr_text):
        errors.append(f"symbol_silently_added_to_clib:{sym}")
if errors:
    print(";".join(errors)); sys.exit(1)
count = len(missing_entries)
if count != m["summary"]["notYetProvided"]:
    print(f"missing_count_mismatch:live={count}:pinned={m['summary']['notYetProvided']}")
    sys.exit(1)
PY
then
  pass "tinycc_libc_deps_missing_set_matches"
else
  fail "tinycc_libc_deps_missing_set_matches" "$(cat /tmp/tinycc_libc_deps.stderr | tr '\n' ';' | cut -c1-200)"
fi

# --- rollup ------------------------------------------------------------------
if [ "$ALL_OK" -eq 1 ]; then
  pass "tinycc_libc_deps"
else
  emit "TEST:FAIL:tinycc_libc_deps:sub_markers_failed"
  exit 1
fi
