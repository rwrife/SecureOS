# BearSSL for SecureOS

## Overview

BearSSL is a compact TLS library (BSD license) designed for embedded/freestanding
environments.  SecureOS uses it to provide TLS 1.2 client support in the user-space
netlib library for HTTPS connections.

## Submodule

BearSSL is included as a git submodule pinned to **v0.6** at `vendor/bearssl/BearSSL/`.

After cloning the repo, initialize the submodule:

```bash
git submodule update --init
```

## Build

Compile BearSSL objects for the SecureOS freestanding i386 target:

```bash
./build/scripts/build_bearssl.sh      # Linux/macOS (uses Docker toolchain)
./build/scripts/build_bearssl.ps1     # Windows (uses Docker toolchain)
```

This produces object files in `artifacts/bearssl/` that are linked into netlib-using
applications automatically by `build_user_app.sh`.

## Files in this directory

| File                  | Purpose                                              |
|-----------------------|------------------------------------------------------|
| `BearSSL/`            | Git submodule — BearSSL v0.6 sources                 |
| `Makefile.secureos`   | Lists the exact BearSSL .c files needed for TLS 1.2  |
| `secureos_compat.c`   | Freestanding libc shims (memcpy, memmove, etc.)      |

## License

BearSSL is licensed under the MIT license.  See `BearSSL/LICENSE.txt`.