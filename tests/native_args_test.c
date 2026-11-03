/**
 * @file tests/native_args_test.c
 * @brief Regression check for native launcher argument delivery bounds.
 *
 * Includes launcher implementation to call its private bridge helper. Built by
 * build/scripts/test_native_args.sh; unused launcher paths are discarded at link.
 */
#include <string.h>
#include "../kernel/user/launcher_exec.c"

int main(void) {
  char buf[5];
  g_native_raw_args = "abcd";
  if (app_native_get_args(buf, sizeof(buf)) != 0 ||
      strcmp(buf, "abcd") != 0) return 1;
  g_native_raw_args = "abcde";
  if (app_native_get_args(buf, sizeof(buf)) == 0 ||
      strcmp(buf, "abcd") != 0) return 2;
  g_native_raw_args = 0;
  if (app_native_get_args(buf, sizeof(buf)) != 0 || buf[0] != '\0') return 3;
  return 0;
}
