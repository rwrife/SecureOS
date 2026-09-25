/*
 * vendor/tinycc/secureos_runtime.c
 *
 * Freestanding runtime compatibility stubs for the SecureOS TinyCC port
 * (issue #766, DEMO-02).
 *
 * Background:
 *   When compiling libtcc core with ONE_SOURCE=0 and TCC_TARGET_X86_64,
 *   upstream libtcc.c:tcc_delete() includes a call to `tcc_run_free(s1)`
 *   guarded by `#ifdef TCC_IS_NATIVE`. In a standard hosted build, this
 *   symbol is defined in `tccrun.c`. However, SecureOS intentionally
 *   excludes `tccrun.c` (-run / JIT mode) to remain fully freestanding
 *   and avoid dynamic execution/mmap dependencies.
 *
 *   This source provides the no-op implementation of `tcc_run_free`
 *   required by `libtcc.c`, allowing `libtcc.a` to link completely
 *   against `libclib.a` with zero undefined compiler symbols.
 */

/* Forward-declare TCCState to avoid pulling in private headers. */
typedef struct TCCState TCCState;

void tcc_run_free(TCCState *s1) {
    (void)s1;
}
