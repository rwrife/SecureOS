# SecureOS File Format (SOF) Implementation Plan

## Summary

This plan introduces the SecureOS File Format (SOF), a container format that
wraps ELF payloads with metadata and code-signing stubs. All executables
become `.bin`, all libraries become `.lib`, and plain data files remain
unformatted. The existing `.elf` extension is retired throughout the
codebase.

See `implementation_plan.md` at the project root for the full technical
specification and implementation order.