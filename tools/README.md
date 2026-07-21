# Tools Drift-Gate Pins

## `disk_image_apps_dev_sha.json` (issue #606)

This JSON pin defines the expected SHA-256 for `/apps/dev/*` staged mappings
from `build/scripts/build_disk_image.sh`.

### When to bump

Bump this file when a **deliberate** change updates any pinned source artifact
content or staging mapping for `/apps/dev/*`.

### Bump procedure

1. Make the intended source or staging-mapping change.
2. Recompute SHA values for changed non-pending entries:
   - `sha256sum dev/hello.c dev/building.txt dev/lib/README.md dev/tcc/README.md`
   - (and any other newly non-pending staged source paths)
3. Update `tools/disk_image_apps_dev_sha.json`:
   - `sha256` for changed entries,
   - `origin` notes if ownership moved,
   - `pending`/`gatingIssue` when a staged artifact is not yet present.
4. Run:
   - `bash build/scripts/test.sh apps_dev_staging`
   - `bash build/scripts/test.sh apps_dev_sha`
5. In the PR body, include before/after hashes for each bumped entry.

### Policy notes

- Unexpected staged targets under `/apps/dev/` are FAIL-fast.
- Missing non-pending source paths are FAIL-fast.
- Pending entries are deferred only while their `gatingIssue` remains OPEN.
