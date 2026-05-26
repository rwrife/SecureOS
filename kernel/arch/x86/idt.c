/**
 * @file idt.c
 * @brief x86-64 IDT initialization and exception handler dispatch.
 *
 * Purpose:
 *   Sets up the 256-entry Interrupt Descriptor Table for x86-64 long mode.
 *   Installs handlers for CPU exceptions 0-31. When a fault occurs during
 *   native app execution (recovery armed), jumps back to the recovery point
 *   instead of crashing the kernel.
 *
 * Interactions:
 *   - idt_stubs.asm: provides the ISR entry stubs and fault_recover functions.
 *   - launcher_exec.c: arms/disarms recovery around native app calls.
 *   - kmain.c: calls idt_init() at boot.
 *
 * Launched by:
 *   idt_init() called from kmain(). Not a standalone binary.
 */

#include "idt.h"
#include "../../hal/serial_hal.h"

/* IDT gate descriptor for x86-64 long mode (16 bytes) */
typedef struct __attribute__((packed)) {
  uint16_t offset_low;      /* Target offset bits 0-15 */
  uint16_t selector;        /* Code segment selector (GDT) */
  uint8_t  ist;             /* IST index (0 = don't use IST) */
  uint8_t  type_attr;       /* Type and attributes (P, DPL, gate type) */
  uint16_t offset_mid;      /* Target offset bits 16-31 */
  uint32_t offset_high;     /* Target offset bits 32-63 */
  uint32_t reserved;        /* Must be zero */
} idt_entry_t;

/* IDT pointer structure for LIDT instruction */
typedef struct __attribute__((packed)) {
  uint16_t limit;
  uint64_t base;
} idt_ptr_t;

/* The IDT itself — 256 entries */
static idt_entry_t idt_entries[256];
static idt_ptr_t   idt_pointer;

/* Last fault info for crash reporting */
static int      g_last_vector;
static uint64_t g_last_rip;
static uint64_t g_last_error_code;

/* External: ISR stubs from idt_stubs.asm */
extern void isr_stub_0(void);
extern void isr_stub_1(void);
extern void isr_stub_2(void);
extern void isr_stub_3(void);
extern void isr_stub_4(void);
extern void isr_stub_5(void);
extern void isr_stub_6(void);
extern void isr_stub_7(void);
extern void isr_stub_8(void);
extern void isr_stub_9(void);
extern void isr_stub_10(void);
extern void isr_stub_11(void);
extern void isr_stub_12(void);
extern void isr_stub_13(void);
extern void isr_stub_14(void);
extern void isr_stub_15(void);
extern void isr_stub_16(void);
extern void isr_stub_17(void);
extern void isr_stub_18(void);
extern void isr_stub_19(void);
extern void isr_stub_20(void);
extern void isr_stub_21(void);
extern void isr_stub_22(void);
extern void isr_stub_23(void);
extern void isr_stub_24(void);
extern void isr_stub_25(void);
extern void isr_stub_26(void);
extern void isr_stub_27(void);
extern void isr_stub_28(void);
extern void isr_stub_29(void);
extern void isr_stub_30(void);
extern void isr_stub_31(void);

/* External: fault recovery from idt_stubs.asm */
extern uint64_t fault_recover_armed;
extern void fault_recover_jump(int return_value);

/* GDT code segment selector (must match entry.asm GDT_CODE_SEL) */
#define GDT_CODE_SEL 0x08

/* IDT gate type: 64-bit interrupt gate, present, DPL=0 */
#define IDT_GATE_PRESENT    0x80
#define IDT_GATE_DPL0       0x00
#define IDT_GATE_INTERRUPT  0x0E  /* 64-bit interrupt gate (clears IF) */
#define IDT_GATE_TRAP       0x0F  /* 64-bit trap gate (keeps IF) */

static void idt_set_gate(int vector, void (*handler)(void), uint8_t type_attr) {
  uint64_t addr = (uint64_t)handler;
  idt_entries[vector].offset_low  = (uint16_t)(addr & 0xFFFF);
  idt_entries[vector].selector    = GDT_CODE_SEL;
  idt_entries[vector].ist         = 0;  /* No IST — use current RSP */
  idt_entries[vector].type_attr   = type_attr;
  idt_entries[vector].offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
  idt_entries[vector].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
  idt_entries[vector].reserved    = 0;
}

/**
 * Exception handler called from isr_common in idt_stubs.asm.
 *
 * Frame layout (pointed to by frame_ptr):
 *   [0]   R15
 *   [8]   R14
 *   [16]  R13
 *   [24]  R12
 *   [32]  R11
 *   [40]  R10
 *   [48]  R9
 *   [56]  R8
 *   [64]  RBP
 *   [72]  RDI
 *   [80]  RSI
 *   [88]  RDX
 *   [96]  RCX
 *   [104] RBX
 *   [112] RAX
 *   [120] Vector number
 *   [128] Error code
 *   [136] RIP (from CPU)
 *   [144] CS  (from CPU)
 *   [152] RFLAGS (from CPU)
 *   [160] RSP (from CPU, always in long mode)
 *   [168] SS  (from CPU, always in long mode)
 */
void idt_exception_handler(uint64_t *frame_ptr) {
  int vector = (int)frame_ptr[15];      /* offset 120 / 8 = 15 */
  uint64_t error_code = frame_ptr[16];  /* offset 128 / 8 = 16 */
  uint64_t rip = frame_ptr[17];         /* offset 136 / 8 = 17 */

  /* Save fault info for crash reporting */
  g_last_vector = vector;
  g_last_rip = rip;
  g_last_error_code = error_code;

  /* If fault recovery is armed, jump back to the save point */
  if (fault_recover_armed != 0) {
    fault_recover_jump(vector + 1);
    /* fault_recover_jump never returns */
  }

  /* Unrecoverable kernel fault — print and halt */
  serial_hal_write("[KERNEL PANIC] Unhandled exception #");
  {
    char num[4];
    int i = 0;
    int v = vector;
    if (v >= 10) { num[i++] = '0' + (v / 10); }
    num[i++] = '0' + (v % 10);
    num[i] = '\0';
    serial_hal_write(num);
  }
  serial_hal_write(" at RIP=0x");
  {
    /* Print RIP in hex */
    char hex[17];
    int i;
    for (i = 15; i >= 0; --i) {
      int nibble = (int)(rip & 0xF);
      hex[i] = (nibble < 10) ? ('0' + nibble) : ('a' + nibble - 10);
      rip >>= 4;
    }
    hex[16] = '\0';
    serial_hal_write(hex);
  }
  serial_hal_write("\n");

  /* Halt */
  for (;;) {
    __asm__ volatile("cli; hlt");
  }
}

void idt_init(void) {
  int i;
  void (*stubs[32])(void);

  /* Zero all entries */
  for (i = 0; i < 256; ++i) {
    idt_entries[i].offset_low = 0;
    idt_entries[i].selector = 0;
    idt_entries[i].ist = 0;
    idt_entries[i].type_attr = 0;
    idt_entries[i].offset_mid = 0;
    idt_entries[i].offset_high = 0;
    idt_entries[i].reserved = 0;
  }

  stubs[0]  = isr_stub_0;
  stubs[1]  = isr_stub_1;
  stubs[2]  = isr_stub_2;
  stubs[3]  = isr_stub_3;
  stubs[4]  = isr_stub_4;
  stubs[5]  = isr_stub_5;
  stubs[6]  = isr_stub_6;
  stubs[7]  = isr_stub_7;
  stubs[8]  = isr_stub_8;
  stubs[9]  = isr_stub_9;
  stubs[10] = isr_stub_10;
  stubs[11] = isr_stub_11;
  stubs[12] = isr_stub_12;
  stubs[13] = isr_stub_13;
  stubs[14] = isr_stub_14;
  stubs[15] = isr_stub_15;
  stubs[16] = isr_stub_16;
  stubs[17] = isr_stub_17;
  stubs[18] = isr_stub_18;
  stubs[19] = isr_stub_19;
  stubs[20] = isr_stub_20;
  stubs[21] = isr_stub_21;
  stubs[22] = isr_stub_22;
  stubs[23] = isr_stub_23;
  stubs[24] = isr_stub_24;
  stubs[25] = isr_stub_25;
  stubs[26] = isr_stub_26;
  stubs[27] = isr_stub_27;
  stubs[28] = isr_stub_28;
  stubs[29] = isr_stub_29;
  stubs[30] = isr_stub_30;
  stubs[31] = isr_stub_31;

  /* Install exception handlers (interrupt gates — clear IF on entry) */
  for (i = 0; i < 32; ++i) {
    idt_set_gate(i, stubs[i], IDT_GATE_PRESENT | IDT_GATE_DPL0 | IDT_GATE_INTERRUPT);
  }

  /* Load IDT */
  idt_pointer.limit = (uint16_t)(sizeof(idt_entries) - 1);
  idt_pointer.base = (uint64_t)&idt_entries[0];
  __asm__ volatile("lidt %0" : : "m"(idt_pointer));
}

/* --- Public query functions --- */

int fault_recover_active(void) {
  return fault_recover_armed != 0;
}

int fault_recover_last_vector(void) {
  return g_last_vector;
}

uint64_t fault_recover_last_rip(void) {
  return g_last_rip;
}
