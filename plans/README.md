# Plans

Canonical home for SecureOS planning documents. See #149 for the
consolidation that made `plans/` the single source of truth (previously
plans were split between `plans/` and `docs/plans/`).

## Conventions

- **Path:** `plans/` at the repo root. The previously-used
  `docs/plans/` directory has been consolidated here.
- **Naming:** `YYYY-MM-DD-<slug>.md`, where the date is the plan's
  authoring date and `<slug>` is a short kebab-case topic identifier
  (e.g. `2026-04-21-capability-broker-share-workflow.md`).
- **Scope:** one plan per topic / vertical slice. Cross-link related
  plans rather than merging.
- **Status:** plans here are historical records of intent and design.
  Active execution status lives in `docs/test-plans/` (milestone +
  task registry, schema-validated per #109) and in the closing PR
  reference inside each plan.

## Index (by topic)

These plans were authored over the M0–M4 build-out and inform the
milestones in `BUILD_ROADMAP.md`. Reviewers tracing a roadmap slice
back to its design doc should start here.

### Boot, kernel, HAL

- `2026-03-16-system-clock-rtc.md`
- `2026-03-17-graphics-boot-scripts.md`
- `2026-03-17-serial-hal-pc-com-driver.md`
- `2026-03-17-video-hal-fallback-backends.md`
- `2026-03-17-video-hal-vga-driver.md`
- `2026-03-17-vgahello-user-app-test-plan.md`

### Filesystem and code-signing

- `2026-03-16-secureos-file-format.md`
- `2026-03-16-code-signing-ed25519-chain.md`
- `2026-03-16-ed25519-i386-verify-fix.md`
- `2026-03-16-cert-expiration-fields.md`
- `2026-04-16-filesystem-service-faux-fs.md`

### Networking and HTTPS

- `2026-03-16-networking-virtio-tcpip-http.md`
- `2026-03-16-network-command-process-extraction.md`
- `2026-03-16-netlib-full-network-stack-extraction.md`
- `2026-03-16-http-ifconfig-standalone-os-apps.md`
- `2026-03-17-netlib-library-extraction-phase1.md`
- `2026-03-17-netlib-https-bearssl.md`
- `2026-04-09-zero-trust-network-abi-hardening.md`

### Console, launcher, capabilities (M2–M4)

- `2026-04-11-console-launcher-capability-slice.md`
- `2026-04-13-console-write-capability-slice.md`
- `2026-04-14-console-service-launcher-helloapp.md`
- `2026-04-18-capability-audit-deny-log.md`
- `2026-04-21-capability-broker-share-workflow.md`
- `2026-05-14-m4-broker-acceptance-tests.md`
- `2026-05-23-m2-on-m1-substrate.md`
- `2026-05-24-m3-fs-on-m1-substrate.md`
- `2026-05-25-m4-broker-on-m1-substrate.md`

## Adding a new plan

1. Create `plans/YYYY-MM-DD-<slug>.md`.
2. Add an entry to the appropriate group above (or open a new group
   if no existing one fits).
3. Reference the plan from the implementing PR.
4. When the plan executes, update the matching milestone entry in
   `docs/test-plans/m0-m1-plan.yaml` (or the milestone's registry
   file) per #109.

## Note on `docs/plans/`

`docs/plans/` was a parallel location that accumulated 6 plans before
the M2 build-out consistently landed new plans here. Per #149 it has
been removed; `git log --follow plans/<file>` still resolves history
for the moved files.
