#include "secureos_api.h"

static void app_log(const char *message) {
  (void)os_console_write(message);
}

int main(void) {
  char list_output[256];
  char file_output[256];

  app_log("[filedemo] start\n");

  if (os_fs_list_root(list_output, sizeof(list_output)) == OS_STATUS_OK) {
    app_log("[filedemo] ls ok\n");
  }

  if (os_fs_read_file("readme.txt", file_output, sizeof(file_output)) == OS_STATUS_OK) {
    app_log("[filedemo] cat ok\n");
  }

  (void)os_fs_write_file("demo.txt", "hello", 0);
  (void)os_fs_write_file("demo.txt", " world", 1);

  app_log("[filedemo] done\n");
  return 0;
}
