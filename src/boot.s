# boot.s - the very first code that runs.
#
# A bootloader (GRUB, or QEMU's built-in loader) finds the "multiboot header"
# below, loads our kernel into memory at 1 MB, switches the CPU into 32-bit
# protected mode and jumps to _start. We set up a stack and call kmain() in C.

.set MB_MAGIC,    0x1BADB002
.set MB_FLAGS,    (1 << 0) | (1 << 1)     # page-align modules, give us memory info
.set MB_CHECKSUM, -(MB_MAGIC + MB_FLAGS)

.section .multiboot, "a"
.align 4
.long MB_MAGIC
.long MB_FLAGS
.long MB_CHECKSUM

.section .bss
.align 16
stack_bottom:
.skip 65536                # 64 KB kernel stack
stack_top:

.section .text
.global _start
.type _start, @function
_start:
    mov  $stack_top, %esp
    cld
    push %ebx               # arg 2: address of the multiboot info structure
    push %eax               # arg 1: multiboot magic number
    call kmain
    cli
1:  hlt                     # kmain should never return, but just in case
    jmp 1b

.section .note.GNU-stack, "", @progbits
