/**
 * @file stdlib.h
 * @brief Freestanding stdlib.h shim for BearSSL.
 *
 * BearSSL references stdlib.h in some code paths but only uses size_t
 * and NULL from it in freestanding builds.
 */
#ifndef _SECUREOS_STDLIB_H
#define _SECUREOS_STDLIB_H

#include <stddef.h>

#endif /* _SECUREOS_STDLIB_H */
