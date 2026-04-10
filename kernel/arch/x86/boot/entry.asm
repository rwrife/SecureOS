; SecureOS kernel entry point
;
; Boot flow:
;   GRUB (Multiboot v1) → _start (32-bit PM) → long mode setup → _start64 (64-bit) → kmain
;
; GRUB hands control in 32-bit protected mode with:
;   EAX = 0x2BADB002 (Multiboot magic)
;   EBX = physical address of Multiboot info structure
;   Interrupts: disabled
;   Paging: disabled
;   GDT: GRUB-supplied flat 32-bit segments
;
; We then:
;   1. Set up a minimal 64-bit GDT
;   2. Build identity-map page tables (first 4MB, 2MB huge pages)
;   3. Enable PAE (CR4.PAE)
;   4. Load CR3 with PML4 physical address
;   5. Set EFER.LME via MSR 0xC0000080
;   6. Enable paging (CR0.PG) → CPU activates long mode
;   7. Far-jump into 64-bit code segment → _start64
;   8. Reload data segments, set 64-bit stack, call kmain

; -----------------------------------------------------------------------
; Multiboot v1 header — must appear in the first 8 KB of the image
; -----------------------------------------------------------------------
MULTIBOOT_MAGIC     equ 0x1BADB002
MULTIBOOT_ALIGN     equ (1 << 0)          ; align loaded modules on page boundaries
MULTIBOOT_MEMINFO   equ (1 << 1)          ; provide memory map
MULTIBOOT_FLAGS     equ MULTIBOOT_ALIGN | MULTIBOOT_MEMINFO
MULTIBOOT_CHECKSUM  equ -(MULTIBOOT_MAGIC + MULTIBOOT_FLAGS)

section .multiboot
align 4
    dd MULTIBOOT_MAGIC
    dd MULTIBOOT_FLAGS
    dd MULTIBOOT_CHECKSUM

; -----------------------------------------------------------------------
; 64-bit GDT
;
; Descriptor format (8 bytes, little-endian):
;   [15:0]  Limit[15:0]
;   [31:16] Base[15:0]
;   [39:32] Base[23:16]
;   [47:40] Access byte  (P | DPL | S | Type)
;   [51:48] Limit[19:16]
;   [55:52] Flags        (G | D/B | L | AVL)
;   [63:56] Base[31:24]
;
; 64-bit code segment: P=1 DPL=0 S=1 Type=0xA L=1 D=0 G=1 → 0x00AF9A000000FFFF
; Flat data segment:   P=1 DPL=0 S=1 Type=0x2 D=1 G=1     → 0x00CF92000000FFFF
; -----------------------------------------------------------------------
section .rodata
align 8
gdt64:
.null:      dq 0x0000000000000000       ; null descriptor
.code:      dq 0x00AF9A000000FFFF       ; 64-bit code  (L=1, D=0)
.data:      dq 0x00CF92000000FFFF       ; flat data
gdt64_ptr:
    dw gdt64_ptr - gdt64 - 1            ; limit (bytes − 1)
    dd gdt64                            ; base (32-bit physical — valid before paging)

; Selector offsets within the GDT above
GDT_CODE_SEL    equ 0x08
GDT_DATA_SEL    equ 0x10

; -----------------------------------------------------------------------
; 32-bit protected-mode entry
; -----------------------------------------------------------------------
[bits 32]
section .text
global _start
extern kmain

_start:
    cli
    mov esp, stack32_top                ; temporary 32-bit stack (in .bss, identity-mapped)

    ; ------------------------------------------------------------------
    ; Build page tables for long mode (identity map 0–4 MB)
    ;
    ; Layout (all in .bss, zero-initialised by GRUB):
    ;   pml4  [0]  → pdpt  (P+W)
    ;   pdpt  [0]  → pd    (P+W)
    ;   pd    [0]  → 0x000000  (P+W+PS, 2 MB huge page)
    ;   pd    [1]  → 0x200000  (P+W+PS, 2 MB huge page)
    ; ------------------------------------------------------------------
    ; PML4[0] = &pdpt | Present | Write
    mov eax, boot_pdpt
    or  eax, 0x3
    mov dword [boot_pml4 + 0], eax
    mov dword [boot_pml4 + 4], 0

    ; PDPT[0] = &pd | Present | Write
    mov eax, boot_pd
    or  eax, 0x3
    mov dword [boot_pdpt + 0], eax
    mov dword [boot_pdpt + 4], 0

    ; PD[0] = 0x000000 | Present | Write | PageSize (2 MB)
    mov dword [boot_pd + 0],  0x000083
    mov dword [boot_pd + 4],  0
    ; PD[1] = 0x200000 | Present | Write | PageSize (2 MB)
    mov dword [boot_pd + 8],  0x200083
    mov dword [boot_pd + 12], 0

    ; ------------------------------------------------------------------
    ; Load the 64-bit GDT (32-bit lgdt is fine here; base is physical)
    ; ------------------------------------------------------------------
    lgdt [gdt64_ptr]

    ; ------------------------------------------------------------------
    ; Enable Physical Address Extension (CR4.PAE, bit 5)
    ; ------------------------------------------------------------------
    mov eax, cr4
    or  eax, (1 << 5)
    mov cr4, eax

    ; ------------------------------------------------------------------
    ; Point CR3 at the PML4
    ; ------------------------------------------------------------------
    mov eax, boot_pml4
    mov cr3, eax

    ; ------------------------------------------------------------------
    ; Set EFER.LME (bit 8) — enables long mode once paging is on
    ; ------------------------------------------------------------------
    mov ecx, 0xC0000080                 ; IA32_EFER MSR
    rdmsr
    or  eax, (1 << 8)
    wrmsr

    ; ------------------------------------------------------------------
    ; Enable paging (CR0.PG, bit 31) — activates long mode
    ; ------------------------------------------------------------------
    mov eax, cr0
    or  eax, (1 << 31)
    mov cr0, eax

    ; ------------------------------------------------------------------
    ; Far jump flushes the pipeline and reloads CS with the 64-bit selector
    ; ------------------------------------------------------------------
    jmp GDT_CODE_SEL:_start64

; -----------------------------------------------------------------------
; 64-bit long-mode entry
; -----------------------------------------------------------------------
[bits 64]
_start64:
    ; Reload data-segment registers (CS already reloaded by far jump)
    mov ax, GDT_DATA_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Switch to the proper 64-bit kernel stack
    mov rsp, stack_top

    ; Call the kernel main function (C linkage, no arguments needed here)
    call kmain

.hang:
    cli
    hlt
    jmp .hang

; -----------------------------------------------------------------------
; BSS: page tables, stacks
; All of these live in the identity-mapped first 4 MB.
; -----------------------------------------------------------------------
section .bss
align 4096
boot_pml4:  resb 4096       ; Page Map Level 4 table (one entry used)
boot_pdpt:  resb 4096       ; Page Directory Pointer Table (one entry used)
boot_pd:    resb 4096       ; Page Directory with 2 MB huge-page entries

; Small 32-bit bootstrap stack (used only during the 32→64 transition)
align 16
stack32_bottom: resb 4096
stack32_top:

; Full 64-bit kernel stack
align 16
stack_bottom:   resb 65536  ; 64 KB
stack_top:
