/**
 * @file native_net_service.h
 * @brief Native user-app networking service bridge for process runtime.
 *
 * Purpose:
 *   Exposes a narrow C interface that the native execution bridge can call
 *   to satisfy user-app networking ABI requests during in-kernel native app
 *   execution.
 *
 * Interactions:
 *   - Uses network_hal for device metadata and frame I/O.
 *   - Does not depend on user-space protocol libraries.
 *
 * Launched by:
 *   Called by process.c native bridge callbacks while executing user apps.
 */

#ifndef SECUREOS_NATIVE_NET_SERVICE_H
#define SECUREOS_NATIVE_NET_SERVICE_H

#include <stddef.h>
#include <stdint.h>

int native_net_device_ready(void);
int native_net_device_backend(char *out_buffer, unsigned int out_buffer_size);
int native_net_device_get_mac(unsigned char *out_buffer, unsigned int out_buffer_size);
int native_net_frame_send(const unsigned char *frame, unsigned int frame_len);
int native_net_frame_recv(unsigned char *out_buffer,
                          unsigned int out_buffer_size,
                          unsigned int *out_frame_len);

#endif
