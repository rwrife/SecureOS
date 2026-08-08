# `/apps/dev/include` Header Layout Convention

Issue: [#617](https://github.com/rwrife/SecureOS/issues/617)

This document pins the authoritative include-path convention for headers staged
under `/apps/dev/include/`.

Why this matters: per `BUILD_ROADMAP.md` §7, include-path spelling is ABI
surface. The exact `#include` line used by apps is part of the contract.

## Rule set

1. **Core OS header remains flat** when sourced from `user/include` root.
2. **Library headers preserve their in-tree namespace prefix** from
   `user/libs/*/include/<libname>/...` whenever the on-disk path can be staged
   under strict FAT 8.3 naming.
3. **When strict 8.3 limits block a direct namespace mirror**, stage the
   smallest documented compatibility alias under `/apps/dev/include/` and keep
   the source-tree authority explicit in this table.
4. Staging work (issues [#613](https://github.com/rwrife/SecureOS/issues/613)
   and [#615](https://github.com/rwrife/SecureOS/issues/615)) MUST follow this
   table instead of introducing ad-hoc flattened names.

## Authoritative mapping

| Staged path under `/apps/dev/include/` | Canonical include spelling in apps | Source-tree authority | Prefix preserved? |
|---|---|---|---|
| `secureos_api.h` | `#include "secureos_api.h"` | `user/include/secureos_api.h` | N/A (flat root header) |
| `sofpack/sofpack.h` | `#include <sofpack/sofpack.h>` | `user/libs/sofpack/include/sofpack/sofpack.h` | Yes |
| `manifest/manifest.h` | `#include <manifest/manifest.h>` | `user/libs/manifestgen/include/manifestgen/manifest_default.h` | 8.3 compatibility alias |

## Compatibility note

Do **not** add flat aliases like `/apps/dev/include/sofpack.h` or
`/apps/dev/include/manifestgen.h` without a dedicated ABI-change issue. Flat
aliases change include-surface expectations and can mask namespace collisions.

`manifest/manifest.h` is a temporary strict-8.3 staging alias for the
`manifestgen/manifest_default.h` source authority. Once long-name support lands
in disk-image/runtime tooling, this table should migrate to the direct
`manifestgen/manifest_default.h` staging path.

Last verified against commit: 959c927d0c20ff887e4b4f38585332da1d6ca4b1
