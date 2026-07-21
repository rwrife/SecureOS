# 2026-07-16 — M7 unblock decomposition rollup (#408 / #409 / #410)

## Goal

Break the three umbrella M7 gating issues into explicitly ordered,
one-session sub-slices so merge work can resume on `main`.

- Gate 1: #408 (TinyCC freestanding)
- Gate 2: #409 (sofpack + cc driver)
- Gate 3: #410 (unsigned-run wiring + acceptance suite)

This is a planning artifact only (no code changes).

## Current snapshot (verified 2026-07-21)

- #408 is **OPEN**
- #409 is **OPEN**
- #410 is **OPEN**

## Decomposition by gating issue

### #408 — TinyCC freestanding port

Ordered sub-slices:

1. [ ] #574 — host `dev/hello.c` compile canary (pre-#408/#409 unblock)
2. [ ] #548 — pin `libtcc1.a` runtime-helper source set + host drift gate
3. [ ] #563 — clib `exit()` shim to `os_process_exit`
4. [ ] #564 — clib `sprintf()` for TinyCC libc-deps gap
5. [ ] #550 — stage `libtcc1.a` under `/apps/dev/tcc/`

**Next-to-land (single pick):** **#574**

Rationale: issue text marks it independent of #408/#409 and explicitly
states it has no open blockers; it is a low-risk fail-fast canary that
reduces misattribution during later #408/#409 flips.

### #409 — sofpack + cc driver app

Ordered sub-slices:

1. [ ] #579 — wire `libmanifestgen.a` into `build_libs()` archive-only opt-in
2. [ ] #540 — `user/apps/cc` driver-app skeleton + staging to `/apps/dev/cc`
3. [ ] #613 — stage `sofpack.h` + `manifestgen.h` to `/apps/dev/include/`
4. [ ] #545 — stage `libclib.a` + `libsofpack.a` under `/apps/dev/lib/`

**Next-to-land (single pick):** **#579**

Rationale: #579 depends on already-closed #533/#535 work, is narrowly
scoped to build/test wiring, and unblocks #540 from linking against a
stable `libmanifestgen.a` archive path.

### #410 — unsigned-run wiring + acceptance suite

Ordered sub-slices:

1. [ ] #546 — pin `os_process_spawn` argv + `out_exit_status` round-trip
2. [ ] #551 — qemu `os_process_exit` status round-trip
3. [ ] #566 — pre-#410 SKIP harness: `toolchain_runs_compiled_binary`
4. [ ] #567 — pre-#410 SKIP harness: `toolchain_compiles_hello_in_os`

**Next-to-land (single pick):** **#546**

Rationale: #546 depends on closed #422/#493, defines the consumer contract
for `cc` invocation shape, and removes ambiguity before #409/#410 harness
flips.

## Gap check

Reviewed against the queue named in #640 and related M7 tracker issues.
No additional mandatory decomposition slices were identified for this
rollup pass.

If new gaps appear during execution, open them as dedicated one-session
issues and append them under the relevant gate in this file.

## Cross-links

- Velocity/process drift context: #620
- Ready-now surfaces that consume this rollup: #626, #627
- Milestone anchor: `BUILD_ROADMAP.md` §5.7 (M7), §9 (risks / agent drift)

## Maintenance note

When any listed sub-slice closes, keep this rollup fresh by:

1. checking the box,
2. updating the ordered remainder under that gate,
3. selecting the new single next-to-land item.
