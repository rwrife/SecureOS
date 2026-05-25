#ifndef SECUREOS_SOSHLIB_H
#define SECUREOS_SOSHLIB_H

/**
 * @file soshlib.h
 * @brief Public include for the sosh scripting language library.
 *
 * Purpose:
 *   Provides applications with access to the sosh interpreter by
 *   forwarding to the library's internal headers. Applications that
 *   want to embed or invoke sosh include this header.
 *
 * Usage:
 *   #include "lib/soshlib.h"
 *   int rc = sosh_run_script(buf, len, "myscript", args, my_output, my_exec, ctx);
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int soshlib_handle_t;

enum {
  SOSHLIB_HANDLE_INVALID = 0u,
};

#ifdef __cplusplus
}
#endif

#endif /* SECUREOS_SOSHLIB_H */
