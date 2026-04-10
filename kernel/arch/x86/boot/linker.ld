/* SecureOS kernel linker script — x86-64 long mode
 *
 * Load base: 1 MB (0x100000) — canonical GRUB/Multiboot load address.
 * The kernel runs identity-mapped (phys == virt) during early boot.
 * All sections must fit within the first 4 MB so that the bootstrap
 * identity-map page tables (two 2 MB huge pages) cover everything.
 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
  . = 1M;

  /* Multiboot header must be in the first 8 KB of the image */
  .multiboot : ALIGN(4K) {
    *(.multiboot)
  }

  .text : ALIGN(4K) {
    *(.text .text.*)
  }

  .rodata : ALIGN(4K) {
    *(.rodata .rodata.*)
  }

  .data : ALIGN(4K) {
    *(.data .data.*)
  }

  .bss : ALIGN(4K) {
    *(COMMON)
    *(.bss .bss.*)
  }

  /DISCARD/ : {
    *(.comment)
    *(.note*)
    *(.eh_frame*)
  }
}
