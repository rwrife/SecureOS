# `list_ready_now_issues.py` help

`tools/list_ready_now_issues.py` surfaces open issues that are likely mergeable
*during a merge stall* (no open dependency refs, docs/CI/stamp style work).

## Usage

```bash
python3 tools/list_ready_now_issues.py
```

Optional flags:

- `--repo owner/name` (default: `rwrife/SecureOS`)
- `--gating-issues 408,409,410,...`
- `--limit 25`
- `--output-json <path>`
- `--issues-file <fixture.json>` (offline fixture mode)
- `--apply-label --label-name ready-now --apply-limit 5` (opt-in write path)

By default, the script is read-only and does **not** mutate GitHub state.
Only `--apply-label` performs issue writes.
