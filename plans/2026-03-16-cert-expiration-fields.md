# Plan: Add Expiration Date to SecureOS Certificates

**Date:** 2026-03-16  
**Status:** Proposed  
**Priority:** Medium  

## Problem

The current `secureos_cert_t` format (132 bytes) has no validity period.
Once a certificate is issued, it is valid forever. This means:
- Compromised intermediate keys cannot be time-bounded
- No certificate rotation enforcement
- No revocation-by-expiration mechanism

## Current Certificate Layout (132 bytes)

| Offset | Size | Field |
|--------|------|-------|
| 0 | 4 | magic ("SCRT") |
| 4 | 32 | issuer_key_hash |
| 36 | 32 | subject_public_key |
| 68 | 64 | signature |

## Proposed Certificate Layout v2 (148 bytes)

| Offset | Size | Field |
|--------|------|-------|
| 0 | 4 | magic ("SCR2") |
| 4 | 32 | issuer_key_hash |
| 36 | 32 | subject_public_key |
| 68 | 8 | not_before (uint64_t, Unix epoch seconds) |
| 76 | 8 | not_after (uint64_t, Unix epoch seconds) |
| 84 | 64 | signature (covers bytes 0–83) |

## Implementation Steps

1. **Add time source to kernel**
   - SecureOS currently has no clock/timer abstraction
   - Need a minimal `kernel_get_time()` HAL function
   - On x86: read RTC (CMOS) or use PIT/HPET for boot-relative time
   - For initial implementation, embed build timestamp as a compile-time constant

2. **Update `secureos_cert_t`**
   - Add `uint64_t not_before` and `uint64_t not_after` fields
   - Update `SECUREOS_CERT_TOTAL_SIZE` from 132 to 148
   - Update magic to "SCR2" for versioning

3. **Update `cert_build()`**
   - Accept not_before/not_after parameters
   - Include them in the signed data region

4. **Update `cert_parse()`**
   - Extract the new fields
   - Support both v1 ("SCRT", 132 bytes) and v2 ("SCR2", 148 bytes) for backwards compatibility

5. **Update `sof_verify_signature()`**
   - After cert validation, check `kernel_get_time()` against not_before/not_after
   - Return new error code `SOF_ERR_CERT_EXPIRED` if outside validity window

6. **Update `about` command**
   - Display cert validity period (not_before, not_after)
   - Show whether cert is currently valid, expired, or not-yet-valid

7. **Update SOF signature section size**
   - Adjust `SECUREOS_CERT_TOTAL_SIZE + ED25519_SIGNATURE_SIZE` calculations

8. **Update build-time signing in `fs_service.c`**
   - Set not_before to build/boot time
   - Set not_after to build/boot time + configurable validity period (e.g., 365 days)

## Dependencies

- Clock/timer HAL (does not exist yet)
- Or: compile-time embedded timestamp as initial approximation

## Risks

- Changing cert size breaks existing signed binaries (mitigate with v1/v2 dual parsing)
- No real-time clock on some embedded targets (mitigate with build-time fallback)