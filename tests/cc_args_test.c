/* Regression check for the in-OS cc driver's argument limit (DEMO-03 #767).
 * Includes the real parser; unused compiler/syscall paths are discarded.
 * Run from the repository root:
 * cc -ffunction-sections -fdata-sections -I user/include \
 *    -I user/libs/clib/include/clib -I user/libs/sofpack/include \
 *    -I user/libs/manifestgen/include -I vendor/tinycc/tinycc \
 *    tests/cc_args_test.c -Wl,--gc-sections -o artifacts/cc_args_test
 * artifacts/cc_args_test
 */
#define main cc_app_main
#include "../user/apps/cc/main.c"
#undef main

int main(void) {
  char exact[] = "a b c d e f g h i j k l m n o p";
  char trailing[] = "a b c d e f g h i j k l m n o p \t\r\n";
  char overflow[] = "a b c d e f g h i j k l m n o p rejected";
  char empty[] = " \t\r\n";
  char *slots[CC_ARGV_MAX];
  if (cc_tokenize(exact, slots, CC_ARGV_MAX) != CC_ARGV_MAX) return 1;
  if (cc_tokenize(trailing, slots, CC_ARGV_MAX) != CC_ARGV_MAX) return 2;
  if (cc_tokenize(overflow, slots, CC_ARGV_MAX) != -1) return 3;
  if (cc_tokenize(empty, slots, CC_ARGV_MAX) != 0) return 4;
  return 0;
}
