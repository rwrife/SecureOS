/**
 * @file console_unsigned_bin_auth_marker_test.c
 * @brief Issue #542 host gate that pins the unsigned-binary authorization
 *        marker strings emitted by `console_authorize_unsigned_binary`
 *        (`kernel/core/console.c`) and the public
 *        `AUTH_TYPE_UNSIGNED_BIN` constant in `user/include/secureos_api.h`.
 *
 * This is a source-contract pin (no runtime behavior change): it reads the
 * authoritative source/header files and asserts the canonical string literals
 * are present byte-for-byte so marker drift cannot land silently.
 *
 * Launched by:
 *   build/scripts/test_console_unsigned_bin_auth_marker.sh
 *   (dispatched by build/scripts/test.sh target
 *    `console_unsigned_bin_auth_marker` and wired into
 *    build/scripts/validate_bundle.sh TEST_TARGETS).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *reason) {
  printf("TEST:FAIL:console_unsigned_bin_auth_marker:%s\n", reason);
  exit(1);
}

static char *read_all_text(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (!fp) {
    return NULL;
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return NULL;
  }
  long sz = ftell(fp);
  if (sz < 0) {
    fclose(fp);
    return NULL;
  }
  if (fseek(fp, 0, SEEK_SET) != 0) {
    fclose(fp);
    return NULL;
  }

  char *buf = (char *)malloc((size_t)sz + 1u);
  if (!buf) {
    fclose(fp);
    return NULL;
  }

  size_t n = fread(buf, 1u, (size_t)sz, fp);
  fclose(fp);
  if (n != (size_t)sz) {
    free(buf);
    return NULL;
  }
  buf[n] = '\0';
  return buf;
}

static void require_contains(const char *haystack,
                             const char *needle,
                             const char *marker) {
  if (strstr(haystack, needle) == NULL) {
    fail(marker);
  }
  printf("TEST:PASS:console_unsigned_bin_auth_marker:%s\n", marker);
}

int main(void) {
  printf("TEST:START:console_unsigned_bin_auth_marker\n");

  char *console_c = read_all_text("kernel/core/console.c");
  if (!console_c) {
    fail("read_console_c");
  }

  char *api_h = read_all_text("user/include/secureos_api.h");
  if (!api_h) {
    free(console_c);
    fail("read_secureos_api_h");
  }

  require_contains(console_c,
                   "console_write(\"\\n[codesign] Unsigned binary check (cached)\\n\");",
                   "cached_header");
  require_contains(console_c,
                   "console_write(\"[codesign] path=\");",
                   "path_line");
  require_contains(console_c,
                   "console_write(\"[codesign] decision=allow (cached)\\n\");",
                   "decision_allow_cached");
  require_contains(console_c,
                   "console_write(\"[codesign] decision=deny (cached)\\n\");",
                   "decision_deny_cached");
  require_contains(console_c,
                   "console_write(\"[codesign] decision=allow (user accepted risk)\");",
                   "decision_allow_prompt");
  require_contains(console_c,
                   "console_write(\"[codesign] decision=deny\\n\");",
                   "decision_deny_prompt");

  require_contains(api_h,
                   "#define AUTH_TYPE_UNSIGNED_BIN  1",
                   "auth_type_unsigned_bin_constant");

  free(console_c);
  free(api_h);

  printf("TEST:PASS:console_unsigned_bin_auth_marker\n");
  return 0;
}
