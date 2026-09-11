# SecureOS Daily Maintenance State

## Run timestamp (UTC)
- 2026-09-11T21:45Z (closeout run; in-flight on the #765 implementation PR)

## Open PR snapshot
- Snapshot moment: post-sync, pre-merge sweep.
- Open PR count at snapshot: **1** — #764 (chore(abi): repoint post-#755
  dangling doc stamps + 2026-09-10 closeout snapshot), non-draft,
  MERGEABLE/CLEAN, all checks green (build-iso-vm-smoke, lint,
  build-and-validate).

## PRs merged this run
- **#764 — chore(abi): repoint post-#755 dangling doc stamps +
  2026-09-10 closeout snapshot** — squash `77620b974f`. All checks
  green. Remote branch deleted; feature worktree removed.
  (Merge initially failed under the env-PAT token with GraphQL
  `Resource not accessible by personal access token`; succeeded with
  the stored gh keychain credential after unsetting GH_TOKEN —
  known cron pitfall.)

## Implementation work this run
- **Selected issue: #765 (DEMO-01 — binary-safe filesystem I/O through
  the native ABI and libc)** — root of the DEMO-01..DEMO-08 chain;
  every other open demo issue (#766..#772) transitively depends on it.
- Branch/PR: `feature/native-binary-fs-io-765` (this snapshot rides on
  that PR branch; see the PR for the full change list).
- Scope implemented:
  - Public ABI + runtime wrappers: `os_fs_read_file_bytes` /
    `os_fs_write_file_bytes` (additive; native-bridge version 4 -> 5,
    append-only slots; `OS_ABI_VERSION` unchanged).
  - Kernel bridge: `app_native_fs_read_file_bytes` /
    `app_native_fs_write_file_bytes` in `kernel/user/launcher_exec.c`
    reuse the existing `fs_read_file_bytes` / `fs_write_file_bytes`
    ops and the exact same CAP_FS_READ / CAP_FS_WRITE +
    CAP_DISK_IO_REQUEST + per-operation disk-IO consent gates as the
    text pair (denied access performs no write).
  - clib fd adapter (`posix_fd.c`): snapshots now read AND flush with
    explicit byte lengths via the new APIs; the strnlen length
    inference and the embedded-NUL flush truncation limitation are
    removed. Text write/append semantics stay compatible (the text
    pair is unchanged and still NUL-terminated).
  - Host gates: extended `clib_posix_fd` with byte-exact leading /
    interior / trailing NUL read + create/append round-trips, and a
    new `native_fs_bytes_wrapper` dynamic bridge-page gate (exact-byte
    payload transport, v5 handshake degrade, full return-code mapping,
    argument guards). Wired into `test.sh`, its usage list, and
    `validate_bundle.sh` TEST_TARGETS.
  - Docs/ABI alignment: `docs/abi/syscalls.md` table (both new calls +
    text-pair limitation notes), `docs/abi/clib-symbols.md` fd-surface
    notes, `vendor/tinycc/libc-deps.json` open-symbol note.
- Local host gates green: `clib_posix_fd`, `native_fs_bytes_wrapper`,
  `abi_version`, `clib_stdio`, `clib_symbol_drift`, `fs_service`,
  `launcher_fs`, `app_runtime`, `mem_brk_wrapper`, `clib_os_brk`,
  `mem_brk_qemu`, `mem_brk_arena_cap_deny`, `process_exit_wrapper`,
  `process_exit_qemu`, `process_spawn_wrapper`,
  `process_spawn_argv_roundtrip`, `sosh_cap_cat_ls`,
  `sosh_cap_write_append`, `validate_sosh_capability_contract`,
  `tinycc_libc_deps`.
- #765 is intentionally NOT closed by this PR: the issue's remaining
  acceptance criteria require in-guest (booted-image) evidence
  (binary write/reopen/compare through the real FAT path, real archive
  read, ELF/SOF persistence at demo sizes, denied-access behavior on
  the native guest path). A `Closes #765` will ride a follow-up
  guest-evidence PR; this PR carries `Refs #765`.

## Open issue snapshot
- Open issue count after this run: **8** (unchanged; the user-created
  DEMO-01..DEMO-08 + SEC-01 chain supersedes the old backlog).
- Remaining open issues: #765 (in progress via PR), #766, #767, #768,
  #769, #770, #771, #772 (dependency chain: 765 -> 766 -> 767 -> 768;
  #765 -> #769; #770/#771 depend on 766-768; #772 is the final
  acceptance gate; #771 is the integration issue).

## Issues closed this run
- None.

## Notes
- No issues/labels/milestones created this run.
- Prior-run merged PRs (#749..#763) already deleted their remote
  branches; this run additionally cleaned the merged #764 worktree and
  local/remote branch.
- Spotted, not filed (report-only): the clib `stdio` on-target backend
  thunks (#404/#411 embedder wiring) still marshal read/write through
  the text pair at the embedder level — when those thunks land they
  should bind the new byte APIs directly; noted in the PR body.
