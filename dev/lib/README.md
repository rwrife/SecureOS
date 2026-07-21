# /apps/dev/lib placeholder

This directory is staged to `/apps/dev/lib/` in the disk image.

It is a placeholder search root for the in-OS TinyCC linker path
(`CONFIG_TCC_LIBPATHS` / `CONFIG_TCC_CRTPREFIX`) and will be populated by
later toolchain slices with archives such as `libclib.a` and `libsofpack.a`.
