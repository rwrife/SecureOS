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
 *   2. vga_init()          – VGA text-mode display
 *   3. cap_table_init()    – Capability table (zero-trust security)
 *   4. event_bus_init()    – Audit event ring buffer
 *   5. Initial cap_grant() – Bootstrap capabilities for subject 0
 *   6. storage_hal_init()  – Storage HAL (ramdisk backend)
 *   7. fs_init()           – In-memory filesystem
 *   8. process subsystem setup  – User application registry and launch paths
 *   9. console_init()      – Interactive console + command loop
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

#include "../arch/x86/vga.h"
#include "../cap/cap_table.h"
#include "../drivers/disk/ata_pio.h"
#include "../drivers/disk/ramdisk.h"
#include "../drivers/network/virtio_net.h"
#include "../drivers/serial/pc_com.h"
#include "../hal/network_hal.h"
#include "../hal/serial_hal.h"
#include "../event/event_bus.h"
#include "../fs/fs_service.h"
#include "../sched/scheduler.h"
#include "session_manager.h"

static const cap_subject_id_t KERNEL_BOOTSTRAP_SUBJECT = 0u;
static const cap_subject_id_t FILEDEMO_SUBJECT = 1u;

__attribute__((used))
void kmain(void) {
  (void)pc_com_serial_init_primary();
  vga_clear();
  cap_table_init();
  event_bus_reset_for_tests();
  if (!ata_pio_init_primary()) {
    ramdisk_init();
  }
    if (!virtio_net_init_primary()) {
      /* virtio-net NIC not found – network HAL remains unregistered */
    }
  fs_service_init();

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

  (void)cap_table_grant(FILEDEMO_SUBJECT, CAP_CONSOLE_WRITE);
  (void)cap_table_grant(FILEDEMO_SUBJECT, CAP_DISK_IO_REQUEST);
  (void)cap_table_grant(FILEDEMO_SUBJECT, CAP_FS_READ);
  (void)cap_table_grant(FILEDEMO_SUBJECT, CAP_FS_WRITE);
  (void)cap_table_grant(FILEDEMO_SUBJECT, CAP_APP_EXEC);

  serial_hal_write("TEST:START:boot_entry\n");
  serial_hal_write("Hello, SecureOS\n");
  vga_write("Hello, SecureOS\n");
  serial_hal_write("KMAIN_REACHED\n");
  serial_hal_write("TEST:PASS:boot_entry\n");

  sched_init();
  session_manager_start(KERNEL_BOOTSTRAP_SUBJECT);
}
