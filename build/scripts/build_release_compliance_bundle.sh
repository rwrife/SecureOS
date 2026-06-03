#!/usr/bin/env bash
# build/scripts/build_release_compliance_bundle.sh
#
# Builds the LGPL-2.1 compliance bundle that MUST accompany every released
# SecureOS image that statically links TinyCC. See
# docs/legal/lgpl-compliance.md for the normative contract.
#
# Tracking issue: #523. Plan:
# plans/2026-05-28-in-os-toolchain-self-hosting.md (compiler choice / licensing).
#
# Output: $OUT_DIR (default artifacts/release/compliance/).
#
# Determinism: tarballs use --sort=name, fixed owner/group, fixed mtime,
# gzip -n — two runs against the same commit produce byte-identical files
# (same discipline as image-determinism, issue #174).
#
# Behaviour when prerequisites are missing:
#   * vendor/tinycc/tinycc/ submodule not initialized → bundle still
#     produced, tinycc-src.tar.gz contains only a README pointer; the
#     CI gate (release_compliance_bundle) SKIPs with `awaiting_408`.
#   * Pre-Phase-3 (no on-image `cc` driver) → relink/secureos-objs.tar.gz
#     contains a placeholder README explaining the relink set is empty
#     until #408 Phase 3 statically links libtcc into the image.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/artifacts/release/compliance}"
TINYCC_SRC="$ROOT_DIR/vendor/tinycc/tinycc"
TINYCC_COPYING="$TINYCC_SRC/COPYING"
BEARSSL_LICENSE="$ROOT_DIR/vendor/bearssl/BearSSL/LICENSE.txt"
COMMIT_SHA="$(git -C "$ROOT_DIR" rev-parse HEAD 2>/dev/null || echo unknown)"

mkdir -p "$OUT_DIR" "$OUT_DIR/relink"

# Helper: deterministic tar.gz of a directory.
det_tar() {
  local src="$1"
  local out="$2"
  local base
  base="$(basename "$src")"
  ( cd "$(dirname "$src")" && \
      tar --sort=name \
          --owner=0 --group=0 --numeric-owner \
          --mtime='UTC 1970-01-01' \
          -cf - "$base" \
      | gzip -n > "$out"
  )
}

# 1) LGPL-2.1 text — byte-identical copy of vendor/tinycc/tinycc/COPYING.
if [[ -f "$TINYCC_COPYING" ]]; then
  cp "$TINYCC_COPYING" "$OUT_DIR/LICENSE.tinycc"
else
  cat >"$OUT_DIR/LICENSE.tinycc" <<EOF
PLACEHOLDER: vendor/tinycc/tinycc/COPYING not present in this checkout.
Run \`git submodule update --init vendor/tinycc/tinycc\` before producing
a release bundle. Tracked by issue #520 (CI submodule init).
EOF
fi

# 2) BearSSL MIT text — byte-identical copy.
if [[ -f "$BEARSSL_LICENSE" ]]; then
  cp "$BEARSSL_LICENSE" "$OUT_DIR/LICENSE.bearssl"
else
  cat >"$OUT_DIR/LICENSE.bearssl" <<EOF
PLACEHOLDER: vendor/bearssl/BearSSL/LICENSE.txt not present in this checkout.
Run \`git submodule update --init vendor/bearssl/BearSSL\` before producing
a release bundle.
EOF
fi

# 3) Attribution file (BearSSL).
cat >"$OUT_DIR/ATTRIBUTION.md" <<'EOF'
# Third-party attribution

This SecureOS release statically links the following third-party libraries.
Full license texts are in the peer files in this directory.

## BearSSL (MIT)

Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>.
Used for TLS 1.2 client support in the userland netlib library.
Vendored at `vendor/bearssl/BearSSL/`; license text in `LICENSE.bearssl`.

## TinyCC (LGPL-2.1)

TinyCC contributors — see `vendor/tinycc/tinycc/RELICENSING` and the
upstream `AUTHORS` / commit log.
Used as the in-OS C compiler backing the `cc` driver app.
Vendored at `vendor/tinycc/tinycc/`; license text in `LICENSE.tinycc`.
Source tarball at this commit shipped as `tinycc-src.tar.gz` (LGPL-2.1
source-availability obligation).
EOF

# 4) SOURCE_URL.txt — canonical SecureOS source pointer.
cat >"$OUT_DIR/SOURCE_URL.txt" <<EOF
SecureOS source repository: https://github.com/rwrife/SecureOS
Release commit:             $COMMIT_SHA

The complete source for this release (including the SecureOS-side glue
that TinyCC links against) is available at the URL + commit above. The
TinyCC submodule pin at this commit is also included as
\`tinycc-src.tar.gz\` in this bundle for direct LGPL-2.1 compliance.
EOF

# 5) tinycc-src.tar.gz — pinned source.
if [[ -d "$TINYCC_SRC" ]] && [[ -n "$(ls -A "$TINYCC_SRC" 2>/dev/null)" ]]; then
  det_tar "$TINYCC_SRC" "$OUT_DIR/tinycc-src.tar.gz"
else
  TMP_PH="$(mktemp -d)"
  mkdir -p "$TMP_PH/tinycc"
  cat >"$TMP_PH/tinycc/README.txt" <<EOF
PLACEHOLDER: vendor/tinycc/tinycc/ submodule not initialized in the source
tree used to build this bundle. Initialize via:

    git submodule update --init vendor/tinycc/tinycc

and rebuild the bundle. The CI gate \`release_compliance_bundle\` SKIPs
with reason \`awaiting_408\` (M7-TOOLCHAIN-005) until the submodule is
present and TinyCC is statically linked into the shipped image.
EOF
  det_tar "$TMP_PH/tinycc" "$OUT_DIR/tinycc-src.tar.gz"
  rm -rf "$TMP_PH"
fi

# 6) relink/ — libtcc.o + SecureOS-side objects + howto.
RELINK_DIR="$OUT_DIR/relink"

# libtcc.o — present once #408 Phase 3 lands; placeholder otherwise.
LIBTCC_O="$ROOT_DIR/artifacts/tinycc/libtcc.o"
if [[ -f "$LIBTCC_O" ]]; then
  cp "$LIBTCC_O" "$RELINK_DIR/libtcc.o"
else
  cat >"$RELINK_DIR/libtcc.o" <<EOF
PLACEHOLDER: artifacts/tinycc/libtcc.o not yet built. M7-TOOLCHAIN-005
(#408) Phase 3 produces this object. Until then the compliance bundle's
CI gate SKIPs with reason \`awaiting_408\`.
EOF
fi

# SecureOS-side objects libtcc.o links against. Pre-Phase-3 the set is
# empty; ship a placeholder tar containing a README explaining why.
TMP_OBJ="$(mktemp -d)"
mkdir -p "$TMP_OBJ/secureos-objs"
SECUREOS_OBJ_DIR="$ROOT_DIR/artifacts/user/apps/cc"
if [[ -d "$SECUREOS_OBJ_DIR" ]] && compgen -G "$SECUREOS_OBJ_DIR/*.o" >/dev/null 2>&1; then
  # Real link set: copy every .o that the cc app link line consumes.
  cp "$SECUREOS_OBJ_DIR"/*.o "$TMP_OBJ/secureos-objs/" 2>/dev/null || true
  # libclib + libos archives that libtcc.o depends on.
  for ar in \
    "$ROOT_DIR/artifacts/user/libs/clib/libclib.a" \
    "$ROOT_DIR/artifacts/user/libs/libos/liblibos.a"; do
    [[ -f "$ar" ]] && cp "$ar" "$TMP_OBJ/secureos-objs/"
  done
else
  cat >"$TMP_OBJ/secureos-objs/README.txt" <<EOF
PLACEHOLDER: SecureOS-side relink set is empty pre-#408 Phase 3 because
libtcc.o is not yet built or linked into the shipped image. Once
M7-TOOLCHAIN-005 (#408) Phase 3 and the cc driver (#409) land, this tar
will contain the exact .o / .a inputs the cc app's link line consumes,
enabling a recipient to swap in a modified libtcc.o and relink an
equivalent image (LGPL-2.1 §6 relink obligation).
EOF
fi
det_tar "$TMP_OBJ/secureos-objs" "$RELINK_DIR/secureos-objs.tar.gz"
rm -rf "$TMP_OBJ"

# Relink howto.
cat >"$RELINK_DIR/README.md" <<'EOF'
# Relinking SecureOS against a modified TinyCC

The LGPL-2.1 license under which TinyCC is distributed requires that the
recipient of a combined work be able to relink the work against a
modified version of TinyCC. This directory contains the exact inputs
needed to do so for this SecureOS release.

## Inputs

- `libtcc.o` — the TinyCC object the shipped image was linked against.
- `secureos-objs.tar.gz` — the SecureOS-side `.o` / `.a` files
  `libtcc.o` was linked against (extracted from this release's
  `artifacts/user/apps/cc/` link line).

## Relink invocation (target shape — exact flags freeze with #408)

```sh
tar xzf secureos-objs.tar.gz
ld -m elf_i386 -nostdlib -static -e _start \
   -o cc.elf libtcc.o secureos-objs/*.o secureos-objs/libclib.a secureos-objs/liblibos.a
```

The resulting `cc.elf` can be wrapped into a SOF using
`build/scripts/build_user_app.sh`'s pack step (or `tools/sof_pack`) and
substituted into a SecureOS disk image to produce an equivalent release.

If the relink inputs above are placeholders (the release predates #408
Phase 3), no relink is required — TinyCC is not yet linked into the
shipped image. The bundle is shipped pre-emptively so the contract and
the CI gate exist before the gating slice lands.
EOF

# Top-level README in the bundle.
cat >"$OUT_DIR/README.md" <<EOF
# SecureOS release compliance bundle

This directory contains the third-party license and source-availability
artifacts required to redistribute the accompanying SecureOS release.

- LGPL-2.1 obligations (TinyCC):  see \`LICENSE.tinycc\`, \`tinycc-src.tar.gz\`, and \`relink/\`.
- MIT attribution (BearSSL):       see \`LICENSE.bearssl\` and \`ATTRIBUTION.md\`.
- Canonical source pointer:        \`SOURCE_URL.txt\`.

See \`docs/legal/lgpl-compliance.md\` in the SecureOS source tree for the
normative description of this bundle.

Release commit: $COMMIT_SHA
EOF

printf 'BUNDLE:OK:%s\n' "$OUT_DIR"
