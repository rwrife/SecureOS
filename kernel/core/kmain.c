#include "../arch/x86/serial.h"
#include "../arch/x86/vga.h"
#include "../cap/cap_table.h"
#include "../drivers/disk/ata_pio.h"
#include "../drivers/disk/ramdisk.h"
#include "../event/event_bus.h"
#include "../fs/fs_service.h"
#include "console.h"

static const cap_subject_id_t KERNEL_BOOTSTRAP_SUBJECT = 0u;
static const cap_subject_id_t FILEDEMO_SUBJECT = 1u;

__attribute__((used))
void kmain(void) {
  serial_init();
  vga_clear();
  cap_table_init();
  event_bus_reset_for_tests();
  if (!ata_pio_init_primary()) {
    ramdisk_init();
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

  (void)cap_table_grant(FILEDEMO_SUBJECT, CAP_CONSOLE_WRITE);
  (void)cap_table_grant(FILEDEMO_SUBJECT, CAP_DISK_IO_REQUEST);
  (void)cap_table_grant(FILEDEMO_SUBJECT, CAP_FS_READ);
  (void)cap_table_grant(FILEDEMO_SUBJECT, CAP_FS_WRITE);

  serial_write("TEST:START:boot_entry\n");
  serial_write("Hello, SecureOS\n");
  vga_write("Hello, SecureOS\n");
  serial_write("KMAIN_REACHED\n");
  serial_write("TEST:PASS:boot_entry\n");

  console_init(KERNEL_BOOTSTRAP_SUBJECT);
  console_run();
}
