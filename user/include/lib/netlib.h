/**
 * @file netlib.h
 * @brief User-space networking library interface for SecureOS commands and apps.
 *
 * Purpose:
 *   Defines the user-space netlib API that command binaries call for
 *   networking operations. Implementations live in user/libs/netlib and run
 *   above raw networking syscalls.
 *
 * Interactions:
 *   - secureos_api.h provides raw device/frame access used by netlib backend.
 *   - user/libs/netlib sources provide DNS/TCP/HTTP and command-facing helpers.
 *
 * Launched by:
 *   Linked into user apps or the standalone netlib shared library.
 */

#ifndef SECUREOS_NETLIB_H
#define SECUREOS_NETLIB_H

#include "secureos_api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int netlib_handle_t;

enum {
  NETLIB_HANDLE_INVALID = 0u,
  NETLIB_BACKEND_NAME_MAX = 16u,
  NETLIB_MAC_LEN = 6u,
  NETLIB_FRAME_MAX = 1518u,
};

typedef enum {
  NETLIB_STATUS_OK = 0,
  NETLIB_STATUS_DENIED = 1,
  NETLIB_STATUS_NOT_FOUND = 2,
  NETLIB_STATUS_ERROR = 3,
} netlib_status_t;

typedef struct {
  int link_up;
  char backend_name[NETLIB_BACKEND_NAME_MAX];
  unsigned char mac[NETLIB_MAC_LEN];
} netlib_interface_info_t;

netlib_status_t netlib_from_os_status(os_status_t status);
netlib_status_t netlib_device_ready(netlib_handle_t handle);
netlib_status_t netlib_get_interface_info(netlib_handle_t handle,
                                          netlib_interface_info_t *out_info);
netlib_status_t netlib_frame_send(netlib_handle_t handle,
                                  const unsigned char *frame,
                                  unsigned int frame_len);
netlib_status_t netlib_frame_recv(netlib_handle_t handle,
                                  unsigned char *out_buffer,
                                  unsigned int out_buffer_size,
                                  unsigned int *out_frame_len);
netlib_status_t netlib_ifconfig(netlib_handle_t handle,
                                char *out_buffer,
                                unsigned int out_buffer_size);
netlib_status_t netlib_http_get(netlib_handle_t handle,
                                const char *url,
                                char *out_buffer,
                                unsigned int out_buffer_size);
netlib_status_t netlib_ping(netlib_handle_t handle,
                            const char *host,
                            char *out_buffer,
                            unsigned int out_buffer_size);

#ifdef __cplusplus
}
#endif

#endif
