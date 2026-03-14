#include "app_runtime.h"

#include "../cap/cap_table.h"
#include "../fs/fs_service.h"

enum {
  APP_OUTPUT_MAX = 512,
};

static int app_string_equals(const char *left, const char *right) {
  while (*left != '\0' && *right != '\0') {
    if (*left != *right) {
      return 0;
    }
    ++left;
    ++right;
  }

  return *left == *right;
}

static void app_emit(const app_runtime_context_t *context, const char *message) {
  if (context != 0 && context->output != 0) {
    context->output(message);
  }
}

size_t app_runtime_list(char *out_buffer, size_t out_buffer_size) {
  static const char apps[] = "filedemo\n";
  size_t index = 0u;

  if (out_buffer == 0 || out_buffer_size == 0u) {
    return 0u;
  }

  while (apps[index] != '\0' && index + 1u < out_buffer_size) {
    out_buffer[index] = apps[index];
    ++index;
  }
  out_buffer[index] = '\0';
  return index;
}

static int app_require_capability(cap_subject_id_t subject_id, capability_id_t capability_id) {
  return cap_table_check(subject_id, capability_id) == CAP_OK;
}

static app_runtime_result_t app_require_storage_access(const app_runtime_context_t *context,
                                                       capability_id_t capability_id,
                                                       const char *operation,
                                                       const char *path) {
  if (!app_require_capability(context->subject_id, capability_id)) {
    return APP_RUNTIME_ERR_CAPABILITY;
  }

  if (!app_require_capability(context->subject_id, CAP_DISK_IO_REQUEST)) {
    return APP_RUNTIME_ERR_CAPABILITY;
  }

  if (context->authorize_disk_io == 0) {
    return APP_RUNTIME_ERR_DENIED;
  }

  if (context->authorize_disk_io(operation, path) != CAP_ACCESS_ALLOW) {
    return APP_RUNTIME_ERR_DENIED;
  }

  return APP_RUNTIME_OK;
}

static app_runtime_result_t app_run_filedemo(const app_runtime_context_t *context) {
  char list_output[APP_OUTPUT_MAX];
  char read_output[APP_OUTPUT_MAX];
  size_t output_len = 0u;
  app_runtime_result_t result = APP_RUNTIME_OK;

  if (context == 0 || context->output == 0) {
    return APP_RUNTIME_ERR_INVALID_ARG;
  }

  if (!app_require_capability(context->subject_id, CAP_CONSOLE_WRITE)) {
    return APP_RUNTIME_ERR_CAPABILITY;
  }

  app_emit(context, "[filedemo] start\n");

  result = app_require_storage_access(context, CAP_FS_READ, "ls", "/");
  if (result != APP_RUNTIME_OK) {
    return result;
  }

  if (fs_list_root(list_output, sizeof(list_output), &output_len) != FS_OK) {
    return APP_RUNTIME_ERR_STORAGE;
  }

  app_emit(context, "[filedemo] ls\n");
  app_emit(context, list_output);

  result = app_require_storage_access(context, CAP_FS_READ, "cat", "readme.txt");
  if (result != APP_RUNTIME_OK) {
    return result;
  }

  if (fs_read_file("readme.txt", read_output, sizeof(read_output), &output_len) != FS_OK) {
    return APP_RUNTIME_ERR_STORAGE;
  }

  app_emit(context, "[filedemo] readme.txt\n");
  app_emit(context, read_output);
  app_emit(context, "\n");

  result = app_require_storage_access(context, CAP_FS_WRITE, "write", "appdemo.txt");
  if (result != APP_RUNTIME_OK) {
    return result;
  }

  if (fs_write_file("appdemo.txt", "filedemo", 0) != FS_OK) {
    return APP_RUNTIME_ERR_STORAGE;
  }

  result = app_require_storage_access(context, CAP_FS_WRITE, "append", "appdemo.txt");
  if (result != APP_RUNTIME_OK) {
    return result;
  }

  if (fs_write_file("appdemo.txt", "-updated", 1) != FS_OK) {
    return APP_RUNTIME_ERR_STORAGE;
  }

  app_emit(context, "[filedemo] wrote appdemo.txt\n");
  app_emit(context, "[filedemo] done\n");
  return APP_RUNTIME_OK;
}

app_runtime_result_t app_runtime_run(const char *app_name, const app_runtime_context_t *context) {
  if (app_name == 0 || context == 0) {
    return APP_RUNTIME_ERR_INVALID_ARG;
  }

  if (app_string_equals(app_name, "filedemo")) {
    return app_run_filedemo(context);
  }

  return APP_RUNTIME_ERR_NOT_FOUND;
}
