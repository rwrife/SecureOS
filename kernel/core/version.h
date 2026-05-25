#ifndef SECUREOS_VERSION_H
#define SECUREOS_VERSION_H

/**
 * @file version.h
 * @brief SecureOS version constants.
 *
 * Purpose:
 *   Defines the human-readable OS version string displayed during boot
 *   and in help/about output. Separate from the ABI version in
 *   user/include/secureos_abi.h which tracks binary compatibility.
 *
 * Interactions:
 *   - core/boot_banner.c: displays version during boot.
 *   - core/console.c: may reference version in welcome or ver command.
 *
 * Launched by:
 *   Header-only; compiled into any translation unit that includes it.
 */

#define SECUREOS_VERSION_MAJOR 0
#define SECUREOS_VERSION_MINOR 1
#define SECUREOS_VERSION_PATCH 0
#define SECUREOS_VERSION "0.1.0"

#endif