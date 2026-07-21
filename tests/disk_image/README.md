# Disk-image drift pins

This directory holds host-side pins used by `build/scripts/test.sh` and
`build/scripts/validate_bundle.sh` to detect unintended drift in `/apps/dev`
disk-image staging.

- `apps_dev_manifest.json` + `tools/validate_apps_dev_staging.py` (issue #570)
  enforce required-path presence for key `/apps/dev/*` artifacts.
- `apps_dev_include_set.json` + `tools/validate_apps_dev_include_set.py`
  (issue #615) enforce the authoritative header-set contract for
  `/apps/dev/include`.

`apps_dev_include_set.json` supports per-header pending gates (`pending: true`
with `gatingIssue`) so the set can be pinned before all headers are staged.
While a gating issue remains open, missing pending headers emit the canonical
SKIP marker; once the issue closes, that header becomes required.
