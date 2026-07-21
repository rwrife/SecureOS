# `/apps/dev/include` Header Layout Convention

Issue: [#617](https://github.com/rwrife/SecureOS/issues/617)

This document pins the authoritative include-path convention for headers staged
under `/apps/dev/include/`.

Why this matters: per `BUILD_ROADMAP.md` §7, include-path spelling is ABI
surface. The exact `#include` line used by apps is part of the contract.

## Rule set

1. **Core OS header remains flat** when sourced from `user/include` root.
2. **Library headers preserve their in-tree namespace prefix** from
   `user/libs/*/include/<libname>/...`.
3. Staging work (issues [#613](https://github.com/rwrife/SecureOS/issues/613)
   and [#615](https://github.com/rwrife/SecureOS/issues/615)) MUST follow this
   table instead of flattening library headers.

## Authoritative mapping

| Staged path under `/apps/dev/include/` | Canonical include spelling in apps | Source-tree authority | Prefix preserved? |
|---|---|---|---|
| `secureos_api.h` | `#include "secureos_api.h"` | `user/include/secureos_api.h` | N/A (flat root header) |
| `sofpack/sofpack.h` | `#include <sofpack/sofpack.h>` | `user/libs/sofpack/include/sofpack/sofpack.h` | Yes |
| `manifestgen/manifest_default.h` | `#include <manifestgen/manifest_default.h>` | `user/libs/manifestgen/include/manifestgen/manifest_default.h` | Yes |

## Compatibility note

Do **not** add flat aliases like `/apps/dev/include/sofpack.h` or
`/apps/dev/include/manifestgen.h` without a dedicated ABI-change issue. Flat
aliases change include-surface expectations and can mask namespace collisions.

Last verified against commit: 82e0cf22b0b8b57a617b70b8340feb9c66068f73
