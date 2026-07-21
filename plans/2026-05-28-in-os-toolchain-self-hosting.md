# 2026-05-28 — In-OS Toolchain (self-hosted app builds)

## Goal

Let a user, **from inside a running SecureOS instance**, write a C source
file and produce a runnable SecureOS binary (`.bin` in SOF form) — the
canonical "compile and run hello world without leaving the OS" milestone.
Today every binary is cross-compiled on the host inside the Docker
toolchain (`build/scripts/build_user_app.sh`) and staged onto the disk
image. This plan covers the path to doing the compile **on the target**.

Concretely, the end state is:

```
sosh> cc /home/hello.c -o /apps/hello.bin
sosh> hello
hello from inside SecureOS
```

This is a **plan only**. Acceptance tests below are the contract for the
follow-up execute issues; no code lands with this document.

## Why not GCC

GCC (and Clang) are non-starters as in-OS compilers:

- GCC needs hundreds of MB of RAM, a full hosted libc, `fork`/`exec`,
  a real filesystem, and a multi-pass driver spawning `cc1`/`as`/`ld`
  as separate processes. SecureOS has none of these.
- Our hard limits make it impossible regardless of effort:
  - **Per-process arena = 1 MB** ([address_space.h:73](kernel/proc/address_space.h#L73),
    `PROC_ARENA_BYTES`), 16 KB of which is kernel stack.
  - **No userland heap** — there is no `malloc`/`sbrk`/`mmap` syscall;
    apps run on static buffers carved from their image.
  - **FS writes cap at 1024 bytes / 2 clusters**
    ([fs_service.c:56](kernel/fs/fs_service.c#L56), `FS_SOF_BUFFER_MAX`).
  - **No userland `exit`/`exec`/`spawn` syscall** — `crt0` traps in
    `hlt` after `main` ([crt0.c:93](sdk/lib/crt0.c#L93)); chaining
    compile→run requires the launcher.

So the work is *less* about "getting GCC" and *more* about (a) growing a
handful of runtime primitives the OS is missing anyway, and (b) porting a
compiler small enough to live within them.

## Compiler choice

Recommendation: **port TinyCC (TCC)**, vendored under `vendor/tinycc/`
following the BearSSL vendor discipline (#117 / PR #122).

| Option | Size / RAM | Codegen | Verdict |
|--------|-----------|---------|---------|
| **TinyCC** | ~100 KB code; compiles in a few MB | x86_64 ELF, built-in assembler **and linker** | **Recommended** — single binary does cc+as+ld; already targets ELF; freestanding-portable |
| chibicc | tiny, readable | x86_64, but **emits asm text → needs external as+ld** | Good teaching base, but we'd have to also port an assembler/linker |
| Write our own | full control | months of work | Out of scope; revisit only if TCC port proves intractable |

TCC wins because it is self-contained: one process turns `.c` into a
linked ELF in memory with no external assembler or linker. That collapses
the "spawn `as`/`ld`" problem the OS can't currently solve.

**Vendoring mechanism — git submodule, mirroring BearSSL.** TinyCC comes
in as a pinned submodule exactly like `vendor/bearssl/BearSSL`
([.gitmodules](.gitmodules), gitlink `160000`):

```
git submodule add -b mob https://github.com/TinyCC/tinycc vendor/tinycc/tinycc
# then pin to a specific commit, as BearSSL pins 8ef7680
```

Our SecureOS-owned files live *beside* the submodule (not inside it),
identical to the BearSSL layout:

```
vendor/tinycc/
  tinycc/             # submodule (gitlink), pinned commit + branch
  Makefile.secureos   # freestanding build rules (the actual porting work)
  README.md           # what/why/pinned-commit
  VERSION
  LICENSE             # see licensing note below
```

**Licensing caveat.** BearSSL is MIT; **TinyCC is LGPL-2.1**. Statically
linking an LGPL compiler into a shipped OS image carries relink / source-
availability obligations that the current permissive deps do not. This is
not a blocker but is a deliberate decision the execute issue must record;
**chibicc (MIT)** is the fallback if the LGPL terms are unacceptable, at
the cost of also porting an assembler + linker (it emits asm text).

The output ELF still needs to be wrapped to SOF and made loadable — see
"In-OS packaging" below.

## Prerequisite runtime slices

The compiler can't run until the OS grows these. Each is independently
useful and testable; sequence them first.

### P1 — Userland dynamic memory (`os_mem_*` + libc `malloc`)
- New syscalls: `os_mem_brk(size)` / `os_mem_map(size, *out)` that extend
  the calling process's arena window.
- **Arena size is not a physical constraint** — there is no hardware cap
  we must respect, so `PROC_ARENA_BYTES` can be raised freely to give TCC
  the several MB it needs. The remaining decision is purely hygiene:
  raise it globally (simplest) vs. a manifest-declared per-app arena so
  only the compiler gets the big window. Recommend per-app to avoid every
  process paying for the compiler's footprint. Either way, raising the
  ceiling alone is **not** sufficient — TCC calls `malloc`, so the
  allocator below is still required.
- Ship a minimal allocator (`dlmalloc`-style or bump+free-list) in a new
  `user/libs/clib/` so the compiler and future apps get `malloc`/`free`.
- ABI: minor bump (additive syscalls). Update `docs/abi/syscalls.md`.

### P2 — Arbitrary-size file writes
- Lift `FS_SOF_BUFFER_MAX` / 2-cluster cap: stream writes through the FAT
  chain so a file of N clusters can be written. Needed to persist a
  multi-KB output binary and to read multi-KB source.
- Keep the existing capability gate (`CAP_FS_WRITE`); this is a size
  change, not a new authority.

### P3 — Freestanding libc subset (`user/libs/clib/`)
- TCC depends on a libc surface: `malloc/free/realloc`, `fopen/fread/
  fwrite/fclose` (over `os_fs_*`), `memcpy/memmove/strcmp/strlen/...`,
  `printf`/`fprintf` (over `os_console_write`), `setjmp/longjmp` (TCC
  uses it for error recovery), `qsort`. No threads, no signals, no locale.
- This is the same library P1's allocator lives in; build it as
  `libclib.a` alongside the SDK's `libos.a`.

### P4 — Userland process control (`os_process_exit`, `os_process_spawn`)
- `os_process_exit(int)` — wire `crt0`'s `_os_exit` to a real syscall so
  the compiler can return a status the shell can read (`$?`), instead of
  faulting on `hlt`.
- `os_process_spawn(path, args, *out_status)` — launch a freshly built
  binary through the existing launcher/broker so `cc … && hello` works.
  Reuses the launcher's manifest + capability flow; no new trust path.
- ABI: minor bump. Gated by `CAP_APP_EXEC` (already exists, added by the
  SOF format work).

## In-OS packaging (ELF → loadable SOF)

The host uses `tools/sof_wrap` to wrap ELF in SOF. We need the equivalent
on-target. Two sub-pieces:

1. **`sof_pack` library** — factor the SOF build logic from
   `kernel/format/sof.c` / `tools/sof_wrap` into a routine callable from
   userland (it's already mostly header-struct writes). The `cc` wrapper
   calls it to emit `/apps/hello.bin` as a valid SOF container around the
   TCC-produced ELF.
2. **Manifest defaulting** — an in-OS build has no `manifest.json`. The
   `cc` tool synthesises a minimal manifest: `owner.kind = "local"`,
   `abi.version` = the running `OS_ABI_VERSION`, `capabilities.required`
   = empty (hello world needs only console write, which it requests at
   runtime). A `--manifest <path>` flag lets the author supply a real one.

## Trust model — the central question

Host-built official binaries are signed via the ed25519 chain
(`2026-03-16-code-signing-ed25519-chain.md`). **An in-OS build cannot
sign with the master key** — the private key is not (and must not be) on
the device. Three tiers, in order of preference:

1. **Unsigned + existing prompt (ship first).** In-OS output is unsigned.
   The loader already raises `AUTH_TYPE_UNSIGNED_BIN`
   ([secureos_api.h:125](user/include/secureos_api.h#L125)); the WM auth
   dialog asks the user to allow it. This is the lowest-risk path and
   needs **no new crypto** — it reuses the unsigned-binary flow end to
   end. "Official" here means "well-formed SOF the OS will run after an
   explicit user grant," not "master-signed."
2. **Per-device local-developer key (follow-up).** Generate a keypair on
   first boot, store the private half in a device-sealed location, and
   sign in-OS builds with it. The loader trusts the local key only for
   binaries built on *this* device, with a distinct trust badge in the
   UI. Lets a user opt out of per-run prompts without forging the master
   chain. Depends on a sealing/keystore primitive we don't have yet.
3. **Master signing (explicitly out of scope).** In-OS builds never carry
   the master signature; promoting a local binary to an official release
   is a host-side, human-reviewed step, unchanged by this plan.

Tier 1 is the milestone deliverable. Tiers 2–3 are noted so the SOF
metadata (`SIG_ALGO`/`SIG_KEYID` fields already reserved in the format)
is laid out to accommodate them without a format break.

## Architecture

```
vendor/tinycc/            # pinned TCC source (vendor discipline)
user/libs/clib/           # malloc + freestanding libc over os_* syscalls
  libclib.a
user/apps/cc/             # the in-OS compiler driver
  main.c                  # arg parse → tcc_compile → sof_pack → write
user/libs/sofpack/        # SOF container builder callable from userland
  libsofpack.a
```

Flow:

```
sosh> cc /home/hello.c -o /apps/hello.bin
  cc/main.c
    ├─ os_fs_read_file("/home/hello.c")        [CAP_FS_READ]   (P2: multi-KB)
    ├─ tcc_compile(src) -> ELF image in heap   (P1 heap, P3 libc)
    ├─ sofpack_wrap(elf, manifest) -> SOF blob
    └─ os_fs_write_file("/apps/hello.bin", …)  [CAP_FS_WRITE]  (P2: multi-KB)
sosh> hello
  launcher loads /apps/hello.bin
    ├─ SOF parse, signature absent
    ├─ AUTH_TYPE_UNSIGNED_BIN prompt -> user Allow
    └─ run under broker-granted caps
```

## Phased implementation

| Phase | Deliverable | Gates on | Status |
|-------|-------------|----------|--------|
| 0 | This plan | — | **done** |
| 1 | Scaffolding: TinyCC submodule (`vendor/tinycc/`), `/apps/dev` disk layout, `hello.c` sample, on-device + in-tree docs, disk-staging wiring + host test | — | **done** |
| 2 | P1 heap + P2 large writes + P4 exit/spawn syscalls | ABI minor bump, `docs/abi` | **done** (#421, #495, #422, #406) |
| 3 | P3 `libclib.a` freestanding libc | P2 | **done** (#407) |
| 4 | `vendor/tinycc/` freestanding port: compiles a `.c` to ELF in memory | P2, P3 | pending (#408) |
| 5 | `user/libs/sofpack/` + `user/apps/cc/`: ELF→SOF→disk, staged to `/apps/dev/cc` | P4, SOF refactor | pending (manifestgen slice #533 merged; driver execution slices #409 + #540 still open) |
| 6 | Unsigned-binary run path wired to `cc` output (Tier 1 trust) + acceptance suite | P5, existing auth flow | pending (#410) |

### Phase 1 — what landed (this branch)

- **TinyCC vendored** as a pinned git submodule at `vendor/tinycc/tinycc`
  (commit `3b1fe97a`), with SecureOS-owned wrapper files beside it
  (`VERSION`, `README.md`, `LICENSE`, `Makefile.secureos`) mirroring the
  BearSSL layout. `Makefile.secureos` enumerates the exact x86_64 `libtcc`
  sources and records the freestanding porting notes for Phase 4.
- **`/apps/dev` developer directory** is created in the default disk layout
  and staged from the repo-level `dev/` directory by
  `build/scripts/build_disk_image.sh`.
- **`dev/hello.c`** — the validation sample; **`dev/building.txt`** — the
  on-device quick-start guide.
- **`docs/in-os-toolchain/building-apps.md`** — full in-tree documentation.
- **`tools/populate_disk_image.py`** now auto-creates parent directories on
  write (so nested `/apps/dev/...` targets need no explicit mkdir).
- **Host test** `tests/in_os_toolchain_dev_dir_test.py` (target
  `in_os_toolchain_dev_dir`) asserts the staging round-trips byte-identically;
  wired into `test.sh` and `validate_bundle.sh`.

Phase 1 ships **no functional compiler** — `cc` and the runtime primitives it
needs are Phases 2–6. The LGPL decision (see `vendor/tinycc/LICENSE`) is
recorded but not yet load-bearing, since nothing links TinyCC yet.

## Acceptance tests

Follow the `TEST:PASS:` / JSON-report contract (#110) under
`tests/m7_toolchain/`. QEMU tests boot the deterministic image.

### `toolchain_compiles_hello_in_os`
- Stage `/home/hello.c` (prints a known string) on the disk image.
- Boot, run `cc /home/hello.c -o /apps/hello.bin`.
- Assert: `cc` exits 0; `/apps/hello.bin` exists and parses as a valid
  SOF wrapping a well-formed x86_64 ELF.

### `toolchain_runs_compiled_binary`
- Run `/apps/hello.bin` (auto-approve the unsigned-binary prompt in the
  test harness).
- Assert: serial log contains the known string; process exits 0 via the
  new `os_process_exit`.

### `toolchain_unsigned_prompt_enforced`
- Run the compiled binary with the prompt set to **deny**.
- Assert: the binary never executes and an audit `launch.denied` record
  with reason `unsigned` is emitted (parity with #84).

### `toolchain_large_output_persisted`
- Compile a source that yields a >2-cluster (>1 KB) binary.
- Assert: the full binary round-trips through `os_fs_write_file` /
  `os_fs_read_file` byte-identically (P2 regression guard).

### `toolchain_compile_error_reported`
- Compile a source with a syntax error.
- Assert: `cc` exits non-zero, prints a diagnostic, writes **no** output
  file, and does not panic the kernel.

### `toolchain_heap_isolation`
- Two sequential `cc` runs in one boot.
- Assert: the second run's allocations do not see the first's state
  (arena reset on process teardown); no leak panics.

## Out of scope

- C++, Rust, dynamic linking, optimisation passes beyond TCC's defaults.
- Master-key signing of in-OS output (Tier 3 above).
- An on-target assembler/linker as separate tools (TCC subsumes them).
- A package manager / `os-pkg install`.
- Self-hosting the *kernel* build in-OS — this is app builds only.

## Open questions for the execute issues

1. **Arena sizing.** Memory is *not* capped (confirmed), so this is a
   hygiene call, not a feasibility one: raise `PROC_ARENA_BYTES` globally
   vs. a manifest-declared per-app arena so only the compiler gets the big
   window. Recommend manifest-declared to avoid bloating every process.
2. **TCC memory ceiling.** What's the largest TU TCC can compile inside
   the chosen arena? The execute issue should measure and document a hard
   cap, failing cleanly (not OOM-panicking) past it. Source-of-truth pin:
   `vendor/tinycc/arena-measurements.json` (issue #543).
3. **libc surface freeze.** Which exact symbols does `libclib.a` export?
   Recommend deriving the set empirically from TCC's link errors, then
   freezing it as an ABI-adjacent contract.
4. **Local-dev key sealing (Tier 2).** Deferred, but where would a sealed
   keystore live given there's no TPM/secure-element model yet? Likely a
   separate "device identity" plan.

## Exit criteria

- Execute issues filed for Phases 1–6 referencing this plan.
- The six acceptance tests above are agreed as the merge gate.
- No code lands in the planning issue.

## Cross-links

- `../docs/release/m7-toolchain-exit-criteria.md` — canonical M7 ship
  checklist and marker-completion tracker (issue #603).
- `2026-05-15-public-sdk-external-app-template.md` — the host build path
  this mirrors on-target; `cc` is the in-OS analogue of `os-cc`+`os-pack`.
- `2026-03-16-secureos-file-format.md` — SOF container that `sofpack`
  reuses; the reserved `SIG_*` metadata keys are the Tier-2/3 hook.
- `2026-03-16-code-signing-ed25519-chain.md` — the master chain in-OS
  builds deliberately do **not** participate in.
- `2026-05-25-scripting-language.md` — `sosh` is the shell that drives the
  compile/run loop; `os_process_spawn` generalises its exec callback.
- `2026-05-25-docker-toolchain-consolidation.md` — the host toolchain that
  remains the path for *official, master-signed* releases.
```
