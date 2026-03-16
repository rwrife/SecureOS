/**
 * @file fs_service_test.c
 * @brief Tests for the FAT-like filesystem service.
 *
 * Purpose:
 *   Validates filesystem initialization, directory listing, file
 *   create/read/write/append, nested directory operations, and
 *   FAT32 boot-sector integrity on a ramdisk-backed storage backend.
 *
 * Interactions:
 *   - fs_service.c: exercises fs_service_init, fs_list_root,
 *     fs_list_dir, fs_read_file, fs_write_file, and fs_mkdir.
 *   - ramdisk.c / storage_hal.c: provides the underlying block device.
 *
 * Launched by:
 *   Compiled and run by the test harness
 *   (build/scripts/test_fs_service.sh).
 */

#include <stdio.h>
#include <stdlib.h>

#include "../kernel/drivers/disk/ramdisk.h"
#include "../kernel/fs/fs_service.h"
#include "../kernel/hal/storage_hal.h"

static void fail(const char *reason) {
  printf("TEST:FAIL:fs_service:%s\n", reason);
  exit(1);
}

static int string_contains(const char *haystack, const char *needle) {
  size_t i = 0u;
  size_t j = 0u;

  if (needle[0] == '\0') {
    return 1;
  }

  while (haystack[i] != '\0') {
    j = 0u;
    while (haystack[i + j] != '\0' && needle[j] != '\0' && haystack[i + j] == needle[j]) {
      ++j;
    }
    if (needle[j] == '\0') {
      return 1;
    }
    ++i;
  }

  return 0;
}

int main(void) {
  char output[512];
  uint8_t boot[512];
  size_t output_len = 0u;

  printf("TEST:START:fs_service\n");

  storage_hal_reset_for_tests();
  ramdisk_init();
  fs_service_init();

  if (storage_hal_read(0u, boot, sizeof(boot)) != STORAGE_OK) {
    fail("boot_sector_read_failed");
  }
  if (!(boot[510] == 0x55 && boot[511] == 0xAA)) {
    fail("boot_signature_invalid");
  }
  printf("TEST:PASS:fs_service_fat32_boot_signature\n");

  if (fs_list_root(output, sizeof(output), &output_len) != FS_OK) {
    fail("list_root_failed");
  }
  if (output_len == 0u) {
    fail("list_root_empty");
  }
  if (!string_contains(output, "os/\n") || !string_contains(output, "apps/\n") ||
      !string_contains(output, "lib/\n")) {
    fail("list_root_missing_system_dirs");
  }
  printf("TEST:PASS:fs_service_list_root\n");

  if (fs_list_dir("/os", output, sizeof(output), &output_len) != FS_OK) {
    fail("list_os_failed");
  }
  if (!string_contains(output, "help.elf\n") || !string_contains(output, "env.elf\n") ||
      !string_contains(output, "libs.elf\n") || !string_contains(output, "loadlib.elf\n") ||
      !string_contains(output, "unload.elf\n")) {
    fail("list_os_missing_help");
  }
  printf("TEST:PASS:fs_service_list_os\n");

  if (fs_list_dir("/lib", output, sizeof(output), &output_len) != FS_OK) {
    fail("list_lib_failed");
  }
  if (!string_contains(output, "envlib.elf\n")) {
    fail("list_lib_missing_envlib");
  }
  printf("TEST:PASS:fs_service_list_lib\n");

  if (fs_read_file("readme.txt", output, sizeof(output), &output_len) != FS_OK) {
    fail("read_seed_file_failed");
  }
  if (output_len == 0u) {
    fail("read_seed_file_empty");
  }
  printf("TEST:PASS:fs_service_read_seed\n");

  if (fs_write_file("demo.txt", "alpha", 0) != FS_OK) {
    fail("write_create_failed");
  }
  if (fs_read_file("demo.txt", output, sizeof(output), &output_len) != FS_OK) {
    fail("read_after_create_failed");
  }
  if (output_len != 5u) {
    fail("read_after_create_len");
  }
  printf("TEST:PASS:fs_service_write_create\n");

  if (fs_write_file("demo.txt", "-beta", 1) != FS_OK) {
    fail("append_failed");
  }
  if (fs_read_file("demo.txt", output, sizeof(output), &output_len) != FS_OK) {
    fail("read_after_append_failed");
  }
  if (output_len != 10u) {
    fail("read_after_append_len");
  }
  if (!(output[0] == 'a' && output[5] == '-' && output[9] == 'a')) {
    fail("read_after_append_content");
  }
  printf("TEST:PASS:fs_service_append\n");

  if (fs_mkdir("scratch") != FS_OK) {
    fail("mkdir_failed");
  }
  if (fs_write_file("scratch/todo.txt", "task", 0) != FS_OK) {
    fail("write_nested_failed");
  }
  if (fs_read_file("scratch/todo.txt", output, sizeof(output), &output_len) != FS_OK) {
    fail("read_nested_failed");
  }
  if (!string_contains(output, "task")) {
    fail("read_nested_unexpected_content");
  }
  printf("TEST:PASS:fs_service_nested_paths\n");

  return 0;
}
