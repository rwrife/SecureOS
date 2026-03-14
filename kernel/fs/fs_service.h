#ifndef SECUREOS_FS_SERVICE_H
#define SECUREOS_FS_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "../hal/storage_hal.h"

typedef enum {
  FS_OK = 0,
  FS_ERR_INVALID_ARG = 1,
  FS_ERR_NOT_FOUND = 2,
  FS_ERR_STORAGE = 3,
  FS_ERR_NO_SPACE = 4,
  FS_ERR_NOT_DIR = 5,
  FS_ERR_ALREADY_EXISTS = 6,
} fs_result_t;

void fs_service_init(void);
fs_result_t fs_list_root(char *out_buffer, size_t out_buffer_size, size_t *out_len);
fs_result_t fs_list_dir(const char *path, char *out_buffer, size_t out_buffer_size, size_t *out_len);
fs_result_t fs_read_file(const char *path, char *out_buffer, size_t out_buffer_size, size_t *out_len);
fs_result_t fs_read_file_bytes(const char *path,
                               uint8_t *out_buffer,
                               size_t out_buffer_size,
                               size_t *out_len);
fs_result_t fs_write_file(const char *path, const char *content, int append);
fs_result_t fs_write_file_bytes(const char *path,
                                const uint8_t *content,
                                size_t content_len,
                                int append);
fs_result_t fs_mkdir(const char *path);

#endif
