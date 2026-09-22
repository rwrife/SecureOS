/*
 * vendor/tinycc/include/config.h — config.h redirect for the freestanding
 * SecureOS TinyCC build (issue #766 / #408 Phase 3, first-compile slice).
 *
 * TinyCC's tcc.h does `#include "config.h"`, which upstream expects the
 * autoconf `./configure` step to generate. We never run `./configure`:
 * the SecureOS build pins the entire configuration in
 * `vendor/tinycc/config-secureos.h` (verified by the `tinycc_config_secureos`
 * gate), and the submodule must stay a verbatim mirror of upstream.
 *
 * This file is the build-tree side of porting note 1 in
 * `vendor/tinycc/Makefile.secureos`: it is discovered as `config.h`
 * through `-I vendor/tinycc/include` and forwards to the pinned config.
 * The upstream tree is never patched.
 */

#ifndef SECUREOS_TINYCC_CONFIG_REDIRECT_H
#define SECUREOS_TINYCC_CONFIG_REDIRECT_H

#include "../config-secureos.h"

#endif /* SECUREOS_TINYCC_CONFIG_REDIRECT_H */
