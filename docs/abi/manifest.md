# SecureOS Launcher Manifest

Status: **draft, `OS_ABI_VERSION=0`**.

At this milestone, the "manifest" is the in-code launcher registration
API plus the explicit grant calls it exposes. The on-disk JSON form is
sketched below but not yet load-bearing; it is the target shape we will
freeze once the launcher-mediated console slice (#82) and filesystem
slice (#83) close.

The launcher is the **only** sanctioned path that widens an app's
capability set. The kernel capability gates do the enforcement, but the
launcher decides whether a given app subject ever gets a grant in the
first place.

## In-code surface (today)

From [`kernel/user/launcher.h`](../../kernel/user/launcher.h):

- `launcher_register_app(app_id, subject_id)` — register an app subject
  with the launcher. Apps not registered cannot be granted any capability
  through the launcher.
- `launcher_grant_console_write(app_id)` /
  `launcher_revoke_console_write(app_id)` — the only sanctioned path to
  widen / narrow `CAP_CONSOLE_WRITE`.
- `launcher_app_console_write(app_id, msg, *bytes_written)` — single app
  output entrypoint; routes through `cap_console_write_gate` so
  deny-by-default still holds.
- `launcher_app_has_console_write(app_id)` — read-only inspection; never
  widens access.

A non-launcher subject that calls the underlying gate directly without
its own explicit grant is denied. The bypass-regression test in
`tests/launcher_console_test.c` proves this.

## Target manifest schema (sketch)

```jsonc
{
  "manifest_version": 0,
  "app": {
    "id": "helloapp",
    "subject_id": 2,
    "binary": "apps/helloapp.bin",
    "signature": "apps/helloapp.bin.sig"
  },
  "capabilities": {
    "request": [
      "CAP_CONSOLE_WRITE"
    ],
    "optional": []
  },
  "launcher": {
    "auto_grant_at_launch": ["CAP_CONSOLE_WRITE"],
    "require_user_confirm": []
  }
}
```

Field semantics we are committing to:

- `manifest_version` is required and must match the current
  `OS_ABI_VERSION` family. Unknown fields are an error, not a warning —
  zero-trust applies to manifest parsing too.
- `app.subject_id` is the `cap_subject_id_t` the launcher will register
  for this app. It must be unique across loaded apps.
- `capabilities.request` lists capabilities the app *may* use. The
  launcher will refuse to grant anything not listed here.
- `capabilities.optional` are capabilities the app can tolerate being
  denied (it must check the `OS_STATUS_DENIED` result and degrade).
- `launcher.auto_grant_at_launch` is the subset the launcher will grant
  unconditionally at startup. Anything in `request` but not in
  `auto_grant_at_launch` must go through `require_user_confirm` (future
  broker slice, #85) or be explicitly granted by an admin.
- Signatures are required for any non-bootstrap app once `CAP_APP_EXEC`
  is enforced end-to-end; today this is bypassable only behind
  `CAP_CODESIGN_BYPASS` in sealed-build mode.

## Worked example: HelloApp (M2)

The HelloApp slice (#82) registers a subject with the launcher, requests
`CAP_CONSOLE_WRITE`, and is exercised under two manifests:

- **Allow manifest** — `auto_grant_at_launch: ["CAP_CONSOLE_WRITE"]`.
  The app prints its banner; the validator emits
  `TEST:PASS:helloapp_console_write_allowed`.
- **Deny manifest** — `auto_grant_at_launch: []`.
  The app's `os_console_write` returns `OS_STATUS_DENIED`; the validator
  emits `TEST:PASS:helloapp_denied_console_write` and a capability-audit
  deny event is recorded (see #92 for the dedicated negative-path test
  and #84 for the audit assertion).

## Compatibility policy

Until `OS_ABI_VERSION` bumps to 1:

- Adding new optional manifest fields is allowed; loaders **must** reject
  unknown fields on `manifest_version: 0` so behavior cannot silently
  drift.
- Adding new capability IDs to `request` is allowed (subject to the rules
  in [capabilities.md](capabilities.md)).
- Renaming a field is **not** allowed without bumping
  `manifest_version`.

Last verified against commit: 9f4f7ccbb19c9ffb28ee4b6de2f3e93c35e65785
