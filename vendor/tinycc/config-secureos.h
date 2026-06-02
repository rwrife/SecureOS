/*
 * vendor/tinycc/config-secureos.h
 *
 * Freestanding build configuration for the in-OS TinyCC port (issue #408,
 * M7-TOOLCHAIN-005). Replaces the autoconf-generated `config.h` that
 * upstream's `./configure` would normally drop into `vendor/tinycc/tinycc/`
 * for a hosted build.
 *
 * STATUS: Phase 2 scaffold (sub-slice of #408). Encodes porting note #1
 * from `vendor/tinycc/Makefile.secureos` as a header the eventual
 * freestanding libtcc build (Phases 3-4 of
 * `plans/2026-05-28-in-os-toolchain-self-hosting.md`) will consume via
 * `-include`. Companion to PR #516's vendor-surface drift gate
 * (`tinycc_vendor_gate`) for the same Makefile.
 *
 * No source under `vendor/tinycc/tinycc/` is patched. This header lives in
 * the SecureOS wrapper directory (`vendor/tinycc/`) so the pinned upstream
 * submodule stays a verbatim mirror of the TinyCC source tree at the SHA
 * recorded in `vendor/tinycc/VERSION`.
 *
 * The eventual Phase 3 build will pass:
 *
 *   clang ... -include vendor/tinycc/config-secureos.h \
 *             -DTCC_VERSION=\"<from-vendor/tinycc/VERSION>\" \
 *             vendor/tinycc/tinycc/libtcc.c ...
 *
 * (or symlink/copy this file as `vendor/tinycc/tinycc/config.h` from the
 * build-tree side, depending on which option Phase 3 picks).
 */

#ifndef SECUREOS_TINYCC_CONFIG_H
#define SECUREOS_TINYCC_CONFIG_H

/*
 * --- Target selection (Makefile.secureos porting note 1) ---
 *
 * SecureOS runs on x86_64 and the only supported on-target object format
 * is ELF. TinyCC derives ELF as the default when neither TCC_TARGET_PE
 * nor TCC_TARGET_MACHO is defined (see vendor/tinycc/tinycc/tcc.h around
 * the `TCC_TARGET_PE` / `TCC_TARGET_MACHO` derivation block), so we
 * positively select x86_64 and leave the ELF default in place.
 */
#define TCC_TARGET_X86_64 1

#if defined(TCC_TARGET_PE) || defined(TCC_TARGET_MACHO)
#  error "config-secureos.h: ELF-only build; TCC_TARGET_PE / TCC_TARGET_MACHO must be unset"
#endif

/*
 * --- Disable hosted-only TinyCC features (Makefile.secureos porting note 1) ---
 *
 * The in-OS compiler:
 *   - Has no `-run` / JIT path (we never link tccrun.c).
 *   - Has no in-binary backtrace support (CONFIG_TCC_BACKTRACE) — SecureOS
 *     userland does not ship the unwinder machinery TinyCC's backtrace
 *     expects.
 *   - Has no bounds-checking instrumentation (CONFIG_TCC_BCHECK) — relies
 *     on host libc/syscalls SecureOS userland does not provide.
 *
 * `CONFIG_TCC_BACKTRACE` and `CONFIG_TCC_BCHECK` are interpreted by tcc.h
 * as "defined → enable" (it then internally normalizes the value), so we
 * deliberately leave them undefined here. The `#error` arms below catch a
 * future config-snippet that accidentally enables them on the SecureOS
 * build.
 */
#ifdef CONFIG_TCC_BACKTRACE
#  error "config-secureos.h: CONFIG_TCC_BACKTRACE must not be defined (porting note 1)"
#endif
#ifdef CONFIG_TCC_BCHECK
#  error "config-secureos.h: CONFIG_TCC_BCHECK must not be defined (porting note 1)"
#endif

/*
 * --- Build mode (Makefile.secureos porting note 1) ---
 *
 * ONE_SOURCE=0 — compile the libtcc translation units separately rather
 * than via the upstream `libtcc.c` umbrella include. The Makefile.secureos
 * `TCC_ALL_SRCS` list assumes per-TU compilation; the drift gate from
 * PR #516 pins that list shape.
 */
#define ONE_SOURCE 0

/*
 * --- Include / library search paths (Makefile.secureos porting note 3) ---
 *
 * TinyCC's default `CONFIG_TCC_SYSINCLUDEPATHS` / `CONFIG_TCC_LIBPATHS`
 * point at host filesystem locations (`/usr/local/include`, `/usr/lib`,
 * ...). On SecureOS the in-OS compiler resolves headers and archives
 * through the VFS under `/apps/dev/`. Pinned here so the eventual port
 * does not silently inherit host paths via the autoconf defaults.
 *
 * Phase 3 owns the VFS plumbing that backs these paths (`fs_open` on the
 * `/apps/dev/include/` and `/apps/dev/lib/` subtrees); this header only
 * defines the strings TinyCC's `tcc_add_sysinclude_path` /
 * `tcc_add_library_path` initializers consume.
 */
#define CONFIG_TCC_SYSINCLUDEPATHS "/apps/dev/include"
#define CONFIG_TCC_LIBPATHS        "/apps/dev/lib"
#define CONFIG_TCC_CRTPREFIX       "/apps/dev/lib"
#define CONFIG_TCC_ELFINTERP       ""   /* SecureOS has no dynamic loader */

/*
 * --- TCCDIR ---
 *
 * TinyCC self-locates resource files (its bundled include shims, runtime
 * helpers) relative to `CONFIG_TCCDIR`. In-OS we ship those under
 * `/apps/dev/tcc/` (Phase 3 places them there via the dev-image populate
 * step; the `in_os_toolchain_dev_dir` host gate already asserts the dir
 * exists in the bundle, see issue #403's scaffold).
 */
#define CONFIG_TCCDIR "/apps/dev/tcc"

/*
 * --- TCC_VERSION ---
 *
 * Phase 3 should pass `-DTCC_VERSION="<contents of vendor/tinycc/VERSION
 * 'Commit:' SHA, short form>"` on the command line; we deliberately do NOT
 * hard-code a version string here so the build picks up submodule pin
 * bumps (caught by PR #516's drift gate) without a header edit.
 *
 * Fallback string so a syntax-only host smoke test (Phase 2: this PR)
 * does not need TCC_VERSION pre-defined.
 */
#ifndef TCC_VERSION
#  define TCC_VERSION "secureos-port-pending-issue-408"
#endif

#endif /* SECUREOS_TINYCC_CONFIG_H */
