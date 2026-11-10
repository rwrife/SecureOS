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

static unsigned int captured_cap;
static unsigned int reported_len;
static os_status_t read_result;

os_status_t os_fs_read_file_bytes(const char *path, void *buf,
                                unsigned int cap, unsigned int *out_len) {
  (void)path;
  (void)buf;
  captured_cap = cap;
  *out_len = reported_len;
  return read_result;
}

int main(void) {
  char exact[] = "a b c d e f g h i j k l m n o p";
  char trailing[] = "a b c d e f g h i j k l m n o p \t\r\n";
  char overflow[] = "a b c d e f g h i j k l m n o p rejected";
  char empty[] = " \t\r\n";
  char *slots[CC_ARGV_MAX];
  char staged[CC_PATH_MAX];
  /* Only guest-legal 8.3 BIN names may reach compiler or manifest I/O. */
  if (!cc_valid_output_path("/apps/12345678.bin") ||
      !cc_valid_output_path("/apps/hello.BIN") ||
      !cc_valid_output_path("/apps/sub/hello.bin") ||
      !cc_valid_output_path("/demo.bin") ||
      cc_valid_output_path("relative/hello.bin") ||
      cc_valid_output_path("/apps/123456789.bin") ||
      cc_valid_output_path("/toolongdir/hello.bin") ||
      cc_valid_output_path("/apps/hello.binx") ||
      cc_valid_output_path("/apps/hello.bin.manifest.json") ||
      cc_valid_output_path("/apps//hello.bin") ||
      cc_valid_output_path("/apps/../hello.bin") ||
      cc_valid_output_path("/apps/hello.bin/")) return 27;
  if (!cc_swap_ext(staged, sizeof(staged), "/apps.v1/demo", "elf") ||
      strcmp(staged, "/apps.v1/demo.elf") != 0) return 5;
  if (!cc_swap_ext(staged, sizeof(staged), "/apps.v1/demo.bin", "elf") ||
      strcmp(staged, "/apps.v1/demo.elf") != 0) return 6;
  if (cc_swap_ext(staged, 8u, "/apps/demo.bin", "elf")) return 7;
  {
    char exact_fit[13];
    if (!cc_swap_ext(exact_fit, sizeof(exact_fit), "12345678.bin", "elf") ||
        strcmp(exact_fit, "12345678.elf") != 0) return 20;
    if (cc_swap_ext(exact_fit, sizeof(exact_fit) - 1u, "12345678.bin", "elf"))
      return 21;
  }
  cc_derive_app_id(staged, sizeof(staged), "/apps/dev/hello.bin");
  if (strcmp(staged, "hello") != 0) return 8;
  cc_derive_app_id(staged, sizeof(staged), "/apps/dev/2-demo.bin");
  if (strcmp(staged, "demo") != 0) return 9;
  cc_derive_app_id(staged, sizeof(staged), "/apps/dev/123.bin");
  if (strcmp(staged, "ccout") != 0) return 10;
  /* Check SOF framing overhead at the launcher's 524288-byte file cap. */
  {
    unsigned char payload = 0;
    sofpack_build_params_t params;
    size_t framed = 0;
    memset(&params, 0, sizeof(params));
    params.elf_payload = &payload;
    params.elf_payload_size = 1u;
    params.file_type = SOFPACK_TYPE_BIN;
    params.name = "hello";
    if (sofpack_wrap_size(&params, &framed) != SOFPACK_OK) return 30;
    params.elf_payload_size = 524288u - (framed - 1u);
    if (!cc_sof_fits_loader(&params)) return 31;
    ++params.elf_payload_size;
    if (cc_sof_fits_loader(&params)) return 32;
  }
  if (cc_tokenize(exact, slots, CC_ARGV_MAX) != CC_ARGV_MAX) return 1;
  if (cc_tokenize(trailing, slots, CC_ARGV_MAX) != CC_ARGV_MAX) return 2;
  if (cc_tokenize(overflow, slots, CC_ARGV_MAX) != -1) return 3;
  if (cc_tokenize(empty, slots, CC_ARGV_MAX) != 0) return 4;
  if (!cc_paths_collide("/apps/demo.c", "/apps/demo.c", NULL)) return 11;
  if (!cc_paths_collide("/apps/demo.c", "/APPS/DEMO.C", NULL)) return 12;
  if (!cc_paths_collide("/apps/demo.elf", "/apps/demo.bin", NULL)) return 13;
  if (!cc_paths_collide("/apps/demo.mft", "/apps/demo.bin", NULL)) return 28;
  if (!cc_paths_collide("/APPS/DEMO.MFT", "/apps/demo.bin", NULL)) return 29;
  if (!cc_paths_collide("/apps/demo.c", "/apps/demo.bin", "/apps/demo.bin")) return 14;
  if (!cc_paths_collide("/apps/demo.c", "/apps/demo.bin", "/apps/demo.elf")) return 15;
  if (cc_paths_collide("/apps/demo.c", "/apps/demo.bin", "/apps/custom.json")) return 16;
  {
    static unsigned char elf[CC_ELF_MAX];
    unsigned int elf_len = 0u; if (sizeof(elf) != CC_ELF_MAX) return 22;
    read_result = OS_STATUS_OK;
    reported_len = CC_ELF_MAX - 1u;
    if (!cc_read_staged_elf("/apps/staged.elf", elf, &elf_len) ||
        elf_len != CC_ELF_MAX - 1u || captured_cap != CC_ELF_MAX) return 23;
    reported_len = CC_ELF_MAX;
    if (cc_read_staged_elf("/apps/staged.elf", elf, &elf_len)) return 24;
    reported_len = 0u;
    if (cc_read_staged_elf("/apps/staged.elf", elf, &elf_len)) return 25;
    read_result = OS_STATUS_DENIED;
    reported_len = 1u;
    if (cc_read_staged_elf("/apps/staged.elf", elf, &elf_len)) return 26;
  }
  {
    char buf[16];
    size_t sz = sizeof(buf);
    reported_len = sizeof(buf);
    read_result = OS_STATUS_OK;
    if (cc_backend_read("/apps/dev/lib/libtcc1.a", buf, &sz, NULL) != CLIB_STDIO_OK ||
        sz != sizeof(buf) || captured_cap != sizeof(buf)) return 17;
    sz = sizeof(buf);
    reported_len = sizeof(buf) + 1u;
    if (cc_backend_read("/apps/dev/lib/libtcc1.a", buf, &sz, NULL) != CLIB_STDIO_ERROR)
      return 18;
    sz = sizeof(buf);
    read_result = OS_STATUS_DENIED;
    reported_len = 0;
    if (cc_backend_read("/apps/dev/lib/libtcc1.a", buf, &sz, NULL) != CLIB_STDIO_ERROR)
      return 19;
  }
  return 0;
}
