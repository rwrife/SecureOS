# SecureOS Plan — M1-CAP-014 Non-delegable capability-admin grant policy

## Why now
M1-CAP-013 added actor-authorized capability mutation, but it still allows any subject that already has `CAP_CAPABILITY_ADMIN` to grant that same admin capability to additional subjects. That creates unrestricted lateral privilege expansion after a single compromise.

To preserve zero-trust-by-default boundaries, admin capability propagation should be explicit and tightly scoped.

## Scope (incremental)
Implement a narrow policy guard:

1. Keep existing mutation authorization (`actor` must hold `CAP_CAPABILITY_ADMIN`).
2. Add a second guard for `CAP_CAPABILITY_ADMIN` grants:
   - only bootstrap root subject (`subject_id=0`) may grant admin to another subject.
3. Keep non-admin capability grants/revokes unchanged.
4. Preserve deterministic audit behavior and existing result/error contract.

## Code targets
- `kernel/cap/capability.c`
  - enforce non-delegable admin-grant policy in `cap_grant_as_for_tests`.
- `tests/cap_api_contract_test.c`
  - add contract tests proving delegated admin grant is denied.
  - add contract tests proving bootstrap-root admin grant remains allowed.
- `docs/planning/M0-M1-task-registry.md`
  - track `M1-CAP-014` lifecycle from `todo` → `in_progress` → `done`.

## Validation
- `./build/scripts/test.sh cap_api_contract`
- `./build/scripts/test.sh capability_table`
- `./build/scripts/validate_bundle.sh`

## Definition of done
- Delegated admin subjects cannot grant `CAP_CAPABILITY_ADMIN`.
- Subject `0` can still bootstrap admin grants.
- Existing non-admin grant/revoke flows remain green.
- CI validations above are green and deterministic.

## Follow-up (out of scope)
- Replace hard-coded bootstrap subject with policy/config source.
- Attach actor identity to audit schema for mutation attribution.
