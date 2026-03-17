# 2026-03-16 Network Command Process Extraction

## Goal

Move the kernel-side `http`, `ping`, and `ifconfig` command implementations out of `kernel/user/process.c` into dedicated source files so the process runner stays focused on generic dispatch.

## Scope

- add dedicated kernel command sources for `http`, `ping`, and `ifconfig`
- add a small networking command dispatcher outside `process.c`
- remove networking command handlers and builtin table entries from `process.c`
- keep shell and PowerShell kernel build scripts in sync

## Notes

- this change does not introduce native execution for standalone ELF apps; it only removes command-specific networking logic from `process.c`
- current command behavior remains kernel-dispatched through the new network command module until the native user-app execution path exists