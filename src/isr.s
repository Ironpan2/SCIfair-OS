# isr.s - interrupt entry points.
#
# When the CPU gets an interrupt (a key press, a timer tick, or an error like
# dividing by zero) it jumps to one of these little stubs. Each stub pushes
# its interrupt number, saves all the registers and calls isr_handler() in C.

.section .text
.extern isr_handler

# CPU exceptions 8, 10-14, 17, 21, 29 and 30 push an error code themselves.
# For all others we push a fake 0 so the stack always looks the same.
.macro ISR num
.global isr\num
isr\num:
.if (\num == 8) || (\num == 10) || (\num == 11) || (\num == 12) || (\num == 13) || (\num == 14) || (\num == 17) || (\num == 21) || (\num == 29) || (\num == 30)
.else
    pushl $0
.endif
    pushl $\num
    jmp isr_common
.endm

.irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
    ISR \n
.endr

isr_common:
    pusha
    pushl %ds
    pushl %es
    pushl %fs
    pushl %gs
    mov  $0x10, %ax          # kernel data segment
    mov  %ax, %ds
    mov  %ax, %es
    mov  %ax, %fs
    mov  %ax, %gs
    cld
    pushl %esp               # pointer to the saved registers (regs_t *)
    call isr_handler
    add  $4, %esp
    popl %gs
    popl %fs
    popl %es
    popl %ds
    popa
    add  $8, %esp            # remove interrupt number + error code
    iret

.section .data
.global isr_stub_table
isr_stub_table:
.irp n, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47
    .long isr\n
.endr

.section .note.GNU-stack, "", @progbits
