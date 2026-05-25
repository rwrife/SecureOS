/**
 * @file kmain.c
 * @brief Kernel entry point – initializes all subsystems and starts the console.
 *
 * Purpose:
 *   Contains kmain(), the C-language entry point of the kernel. Responsible
 *   for initializing all kernel subsystems in the correct order and then
 *   entering the interactive console loop. This is the orchestration hub
 *   that brings up the entire operating system.
 *
 * Initialization order:
 *   1. pc_com_serial_init_primary() – default serial backend registration
 *   2. vga_text_init_primary() – default video backend registration
 *   3. cap_table_init()    – Capability table (zero-trust security)
 *   4. event_bus_init()    – Audit event ring buffer
 *   5. Initial cap_grant() – Bootstrap capabilities for subject 0
 *   6. storage_hal_init()  – Storage HAL (ramdisk backend)
 *   7. fs_init()           – In-memory filesystem
 *   8. ipc_port_table_init() + console_svc_init() + fs_svc_init() +
 *       broker_svc_init()
 *       – IPC port table and in-kernel substrate services
 *         (issues #283, #302)
 *   9. process subsystem setup  – User application registry and launch paths
 *  10. console_init()      – Interactive console + command loop
 *
 * Interactions:
 *   - Calls init functions from every major kernel subsystem.
 *   - After initialization, enters the console loop which handles all
 *     further user interaction and command dispatch.
 *
 * Launched by:
 *   Called from the assembly boot entry point (kernel/arch/x86/boot/entry.asm)
 *   after the CPU is set up in protected/long mode and the stack is ready.
 */

#include "../cap/cap_table.h"
#include "../drivers/clock/cmos_rtc.h"
#include "../drivers/disk/ata_pio.h"
#include "../drivers/disk/ramdisk.h"
#include "../drivers/network/virtio_net.h"
#include "../drivers/video/framebuffer_text_stub.h"
#include "../drivers/video/gpio_text_stub.h"
#include "../drivers/serial/pc_com.h"
#include "../drivers/video/vga_text.h"
#include "../clock/clock_service.h"
#include "../hal/network_hal.h"
#include "../hal/serial_hal.h"
#include "../hal/video_hal.h"
#include "../event/event_bus.h"
#include "../fs/fs_service.h"
#include "../ipc/ipc_port.h"
#include "../sched/scheduler.h"
#include "../svc/broker_svc.h"
#include "../svc/console_svc.h"
#include "../svc/fs_svc.h"
#include "session_manager.h"

static const cap_subject_id_t KERNEL_BOOTSTRAP_SUBJECT = 0u;
static const cap_subject_id_t FILEDEMO_SUBJECT = 1u;

__attribute__((used))
void kmain(void) {
  (void)pc_com_serial_init_primary();
  if (!vga_text_init_primary()) {
    if (!framebuffer_text_stub_init_primary()) {
      (void)gpio_text_stub_init_primary();
    }
  }
  video_hal_clear();
  cap_table_init();
  event_bus_reset_for_tests();
  if (!ata_pio_init_primary()) {
    ramdisk_init();
  }
    if (!virtio_net_init_primary()) {
      /* virtio-net NIC not found – network HAL remains unregistered */
    }
  cmos_rtc_init();
  clock_service_init();
  fs_service_init();

  /* Bring up IPC port table and the in-kernel substrate services. These
   * must run after the port table is initialised; see issue #283 for
   * the boot-order edge that closes the header/kmain drift documented
   * in kernel/svc/{console_svc,fs_svc}.h. */
  ipc_port_table_init();
  if (console_svc_init() != CONSOLE_SVC_OK) {
    serial_hal_write("TEST:FAIL:console_svc_init\n");
  } else {
    serial_hal_write("TEST:PASS:console_svc_init\n");
  }
  if (fs_svc_init() != FS_SVC_OK) {
    serial_hal_write("TEST:FAIL:fs_svc_init\n");
  } else {
    serial_hal_write("TEST:PASS:fs_svc_init\n");
  }
  if (broker_svc_init() != BROKER_SVC_OK) {
    serial_hal_write("TEST:FAIL:broker_svc_init\n");
  } else {
    serial_hal_write("TEST:PASS:broker_svc_init\n");
  }

  (void)cap_table_grant(KERNEL_BOOTSTRAP_SUBJECT, CAP_CONSOLE_WRITE);
  (void)cap_table_grant(KERNEL_BOOTSTRAP_SUBJECT, CAP_SERIAL_WRITE);
  (void)cap_table_grant(KERNEL_BOOTSTRAP_SUBJECT, CAP_DEBUG_EXIT);
  (void)cap_table_grant(KERNEL_BOOTSTRAP_SUBJECT, CAP_DISK_IO_REQUEST);
  (void)cap_table_grant(KERNEL_BOOTSTRAP_SUBJECT, CAP_FS_READ);
  (void)cap_table_grant(KERNEL_BOOTSTRAP_SUBJECT, CAP_FS_WRITE);
  (void)cap_table_grant(KERNEL_BOOTSTRAP_SUBJECT, CAP_EVENT_SUBSCRIBE);
  (void)cap_table_grant(KERNEL_BOOTSTRAP_SUBJECT, CAP_EVENT_PUBLISH);
  (void)cap_table_grant(KERNEL_BOOTSTRAP_SUBJECT, CAP_APP_EXEC);
  (void)cap_table_grant(KERNEL_BOOTSTRAP_SUBJECT, CAP_NETWORK);
  (void)cap_table_grant(KERNEL_BOOTSTRAP_SUBJECT, CAP_IPC_SEND);
  (void)cap_table_grant(KERNEL_BOOTSTRAP_SUBJECT, CAP_IPC_RECV);
  (void)cap_table_grant(KERNEL_BOOTSTRAP_SUBJECT, CAP_CLOCK_SET);

  (void)cap_table_grant(FILEDEMO_SUBJECT, CAP_CONSOLE_WRITE);
  (void)cap_table_grant(FILEDEMO_SUBJECT, CAP_DISK_IO_REQUEST);
  (void)cap_table_grant(FILEDEMO_SUBJECT, CAP_FS_READ);
  (void)cap_table_grant(FILEDEMO_SUBJECT, CAP_FS_WRITE);
  (void)cap_table_grant(FILEDEMO_SUBJECT, CAP_APP_EXEC);

  serial_hal_write("TEST:START:boot_entry\n");
  serial_hal_write("Hello, SecureOS\n");
  video_hal_write("Hello, SecureOS\n");
  serial_hal_write("KMAIN_REACHED\n");
  serial_hal_write("TEST:PASS:boot_entry\n");

  sched_init();
  session_manager_start(KERNEL_BOOTSTRAP_SUBJECT);
}
