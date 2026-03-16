# Plan: `about` CLI Command & soflib User-Space Library

**Date:** 2026-03-16
**Status:** In Progress

## Goal

Create a new `about <file>` CLI command that reads a `.bin` or `.lib` file
from the filesystem, parses its SOF container header and TLV metadata, and
displays the file's metadata (name, description, author, version, date,
file type, signature status).

Also create a user-space library (`soflib`) that provides an API for
extracting SOF metadata from raw file bytes, following the same pattern as
`envlib` and `fslib`.

Ensure all OS binaries created in `fs_service_init()` have proper,
unique description metadata populated (currently `description` is set
to the same value as `name`).

## Deliverables

1. **`user/include/lib/soflib.h`** — Header-only user-space library for SOF
   metadata extraction. Provides `soflib_parse()` and `soflib_get_meta()`
   helpers that wrap the kernel's SOF format definitions for user-space
   consumption.

2. **`user/libs/soflib/main.c`** — Marker source file for the soflib
   loadable library artifact.

3. **`user/apps/os/about/main.c`** — Source file for the `about` OS command.

4. **`kernel/user/process.c`** — Add `about` script command handler that
   reads a file, parses SOF metadata, and emits formatted output.

5. **`kernel/fs/fs_service.c`** — Update `fs_service_init()` to:
   - Register `about.bin` in `/os/`
   - Register `soflib.lib` in `/lib/`
   - Populate proper descriptions for every existing OS command
   - Update `fs_build_sof_binary()` and `fs_build_sof_library()` to
     accept a description parameter

## Implementation Notes

- The `about` script command is `about $1\n` — it takes a single filename
  argument.
- The kernel-side handler reads the file via `fs_read_file_bytes()`,
  parses it with `sof_parse()`, then emits each metadata field.
- File type is displayed as "binary" / "library" / "app" / "unknown".
- Signature status is displayed as "unsigned" / "signed".
- The `fs_build_sof_binary` and `fs_build_sof_library` helpers are
  updated to accept a description parameter instead of duplicating name.