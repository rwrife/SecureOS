# LGPL-2.1 Compliance Bundle for Shipped SecureOS Images

Owner: release / vendor maintenance
Status: draft v0 (scaffold; SKIP-pinned until M7-TOOLCHAIN Phase 3 lands)
Last reviewed: 2026-06-02
Applies to: any released SecureOS disk image / ISO that **statically links
TinyCC** (`libtcc.o`) into the shipped artifact (Phase 3 of
[`plans/2026-05-28-in-os-toolchain-self-hosting.md`](../../plans/2026-05-28-in-os-toolchain-self-hosting.md)
onward — i.e. the moment `#408` Phase 3 / `#409` / `#410` produce an image
containing a working in-OS `cc`).
Tracking issue: [#523](https://github.com/rwrife/SecureOS/issues/523)

---

## 1. Background

SecureOS ships two third-party libraries that are statically linked into
released disk images:

| Library | License | Vendored at                                   | Obligations summary               |
| ------- | ------- | --------------------------------------------- | --------------------------------- |
| TinyCC  | LGPL-2.1 | `vendor/tinycc/tinycc/` (submodule, see `vendor/tinycc/VERSION`) | Source-availability + **relink** |
| BearSSL | MIT      | `vendor/bearssl/BearSSL/` (submodule, see `vendor/bearssl/VERSION`) | Attribution + license-text copy   |

TinyCC's LGPL-2.1 terms are recorded in `vendor/tinycc/LICENSE`. The
decision to accept those terms (rather than switching to chibicc + porting
an assembler + linker) is recorded in
[`plans/2026-05-28-in-os-toolchain-self-hosting.md`](../../plans/2026-05-28-in-os-toolchain-self-hosting.md),
section **"Compiler choice"** / **"Licensing caveat"**. That plan
explicitly defers the **shipping** side ("a deliberate decision the execute
issue must record") to this document.

BearSSL's MIT terms are unrelated to TinyCC's; they are documented here
only because the same release bundle is the natural carrier for both
attribution and the LGPL relink artifacts.

## 2. LGPL-2.1 obligations (restated)

From `vendor/tinycc/LICENSE`, normative on every release that includes
TinyCC:

> The combined work may be distributed, but recipients must be able to
> relink against a modified TinyCC. In practice this means shipping the
> TinyCC object files (or this pinned source) and the SecureOS-side
> object files needed to relink.
>
> Any modifications to TinyCC's own source must be made available under
> the LGPL.

This implies two distribution-time requirements:

1. **Source availability.** Either ship TinyCC source at the pinned commit
   alongside the image, or include a clear written offer (per LGPL-2.1 §3)
   pointing at a long-lived URL that serves the pinned-commit source. The
   SecureOS project elects to ship the source **inside the compliance
   bundle**, so no separate offer is required.
2. **Relink capability.** Ship the TinyCC object files **and** the
   SecureOS-side object files those TinyCC objects link against, so a
   recipient can rebuild TinyCC (with patches) and relink against the
   shipped SecureOS objects to produce an equivalent image.

Modifications to TinyCC itself (if any future SecureOS patch touches
`vendor/tinycc/tinycc/*.c` rather than only `vendor/tinycc/*.secureos*`
shims) MUST be carried as upstreamable patches in `vendor/tinycc/patches/`
and re-distributed under the LGPL as part of the source tarball.

## 3. Compliance bundle contract

Every released SecureOS image that statically links TinyCC MUST be
accompanied by a **compliance bundle** with the layout below, produced
by `build/scripts/build_release_compliance_bundle.sh` into
`artifacts/release/compliance/`:

```
artifacts/release/compliance/
├── README.md                       # human-readable index + relink howto
├── LICENSE.tinycc                  # byte-identical copy of vendor/tinycc/tinycc/COPYING (LGPL-2.1)
├── LICENSE.bearssl                 # byte-identical copy of vendor/bearssl/BearSSL/LICENSE.txt (MIT)
├── ATTRIBUTION.md                  # BearSSL attribution per MIT license
├── SOURCE_URL.txt                  # canonical SecureOS git URL + commit SHA of the release
├── tinycc-src.tar.gz               # vendor/tinycc/tinycc/ at the pinned commit (deterministic tar)
└── relink/
    ├── README.md                   # exact `tcc -o ...` relink invocation
    ├── libtcc.o                    # the TinyCC object the image was linked against
    └── secureos-objs.tar.gz        # the SecureOS-side objects libtcc.o was linked against (deterministic tar)
```

Each filename and the bundle layout itself are normative — the
`release_compliance_bundle` host test (see §5) asserts shape and
byte-identity of the license-text files.

### 3.1 Determinism

The bundle is deterministic in the same sense as the disk image
([`check_image_determinism.sh`](../../build/scripts/check_image_determinism.sh),
issue #174):

- `tinycc-src.tar.gz` and `secureos-objs.tar.gz` are built with
  `tar --sort=name --owner=0 --group=0 --numeric-owner --mtime='UTC
  1970-01-01' | gzip -n` so two runs against the same commit produce
  byte-identical archives.
- All other files are direct byte-copies of in-tree sources.
- A second invocation against the same commit MUST hash identically to
  the first.

### 3.2 SecureOS-side relink objects

The "SecureOS-side object files needed to relink" are the minimal set of
`user/libs/clib/*.o` + `user/libs/libos/*.o` that `libtcc.o` references as
undefined symbols. The release script enumerates them from the actual link
line used to build the `cc` app (`user/apps/cc/`), so the bundle stays in
sync with what the image actually contains — no manual list to drift.

While `#408` Phase 3 is not yet landed and `cc` is not in the image, the
script writes a placeholder `secureos-objs.tar.gz` containing only a
`README.txt` explaining that the relink set is empty pre-Phase-3, and the
host test (§5) SKIPs with reason `awaiting_408`.

## 4. README + vendor cross-references

- Top-level [`README.md`](../../README.md) — under "Distributing SecureOS
  images", points at `docs/legal/lgpl-compliance.md`.
- [`vendor/tinycc/README.md`](../../vendor/tinycc/README.md) — under
  "Legal", links here for the shipping-side obligations (the in-tree
  `LICENSE` file already covers the source-side decision record).
- [`vendor/bearssl/README.md`](../../vendor/bearssl/README.md) — under
  "License", notes the MIT attribution shows up in
  `artifacts/release/compliance/ATTRIBUTION.md`.

## 5. CI gate

`build/scripts/test.sh release_compliance_bundle` (wired into
`validate_bundle.sh` `TEST_TARGETS`):

- Runs `build_release_compliance_bundle.sh` into a scratch directory.
- Asserts every filename listed in §3 exists.
- Asserts `LICENSE.tinycc` is byte-identical to
  `vendor/tinycc/tinycc/COPYING` and `LICENSE.bearssl` is byte-identical
  to `vendor/bearssl/BearSSL/LICENSE.txt` (when those submodules are
  initialized).
- Today the gate emits `TEST:SKIP:release_compliance_bundle:awaiting_408`
  and rolls up `TEST:PASS:release_compliance_bundle` (same SKIP discipline
  as `tests/m7_toolchain/`), so the bundle stays green until `#408`
  Phase 3 actually links TinyCC into a shipped image.

### 5.1 SKIP marker tracking + flip protocol

| Marker | Tracked by | Current policy |
| --- | --- | --- |
| `TEST:SKIP:release_compliance_bundle:awaiting_408` | [#553](https://github.com/rwrife/SecureOS/issues/553) | Allowed only while #408 Phase 3 is open |

Flip protocol (must happen in the same PR that lands #408 Phase 3, or an
immediate follow-up):

1. Remove `TEST:SKIP:release_compliance_bundle:awaiting_408` from
   `build/scripts/test_release_compliance_bundle.sh`.
2. Update this section to remove `awaiting_408` wording.
3. Run `build/scripts/test_release_compliance_bundle_skip_pinned.sh`
   with `--strict-no-skip` (or
   `RELEASE_COMPLIANCE_BUNDLE_STRICT_NO_SKIP=1`) so stale SKIP markers in
   either script or docs fail the gate.

After the flip, `release_compliance_bundle` is required-PASS — any release
that forgets to build the compliance bundle flips the bundle to FAIL.

## 6. Out of scope

- Re-licensing TinyCC or replacing it with chibicc.
- User-facing distribution UX ("how does a SecureOS user download the
  compliance bundle?") — separate distribution-channel question.
- Runtime enforcement (this is a release-time gate, not a runtime gate).

## Provenance

Last verified against commit: a43ef3d
