/**
 * @file main.c
 * @brief Minimal external-app sample for the SecureOS public SDK.
 *
 * Purpose:
 *   Skeleton companion to the M6 SDK scaffold (plan
 *   `plans/2026-05-15-public-sdk-external-app-template.md`, BUILD_ROADMAP
 *   §5.6, umbrella issue #396 / slice M6-SDK-003). The plan's
 *   §"Sample External App: `hello-from-sdk`" calls for a tiny external
 *   app whose `main.c` "calls console_write("hello from sdk\n") and
 *   exits 0" and whose `manifest.json` declares only `CAP_CONSOLE_WRITE`.
 *
 *   This file lands the source skeleton ahead of the `os-cc` / `os-pack`
 *   / `os-run` host wrappers, which are gated on the maintainer A/B
 *   design decision tracked in the body of #396. Until those wrappers
 *   exist, this sample is NOT built by default CI — the
 *   `sdk_external_build_isolation` acceptance test (also in #396,
 *   deferred) is what will eventually copy this directory to a scratch
 *   dir outside the repo and build it from there using the SDK
 *   wrappers, with no `-I` / `-L` reaching back into the source tree.
 *
 * Containment / SDK contract:
 *   - Includes ONLY public SDK headers under `sdk/include/os/`. No
 *     `kernel/` includes, and no in-tree `user/include/` paths — the
 *     `validate_sdk_no_kernel_includes` CI check is the existing
 *     enforcement point for the kernel half of that rule; the
 *     `sdk_external_build_isolation` test (deferred) will enforce the
 *     in-tree-`user/`-headers half.
 *   - `os_console_write` is forward-declared here against the public
 *     `OS_ABI_VERSION` surface (see `os/abi.h`). The eventual
 *     `sdk/include/os/console.h` header (additive, not yet shipped) will
 *     replace this local prototype; that header is part of the wrapper
 *     slice (#396) since it changes the SDK public surface.
 *
 * Launched by:
 *   Out-of-tree only, via the future `os-cc` / `os-pack` / `os-run`
 *   wrappers. Never compiled by the in-tree build today.
 */

#include "os/abi.h"

/*
 * Forward declaration against the OS_ABI_VERSION=0 syscall surface. This
 * mirrors the in-tree spelling at `user/include/secureos_api.h`:
 *
 *   os_status_t os_console_write(const char *message);
 *
 * The SDK's public re-export of this prototype lives in a follow-up
 * header (`sdk/include/os/console.h`) that the wrapper slice (#396)
 * will add; until then, declaring it locally keeps this sample
 * self-contained and SDK-only at the include level.
 *
 * `os_status_t` is `int`-shaped in the in-tree headers; we mirror that
 * here so the link-time signature matches once `libos.a` provides the
 * stub. `0` is the success status.
 */
typedef int os_status_t;
os_status_t os_console_write(const char *message);

int main(void) {
  (void)os_console_write("hello from sdk\n");
  return 0;
}
