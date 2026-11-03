; Violyx 64-Bit-Einstieg und Interrupt-Stubs. Limine uebergibt im Long Mode.
default rel
section .text
global _start, gdt_load
extern kmain, isr_handler
_start:
    lea rsp, [stack_top]
    cld
    call kmain
.h: cli
    hlt
    jmp .h

gdt_load:                   ; rdi = Zeiger auf GDTR
    lgdt [rdi]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor ax, ax
    mov fs, ax
    mov gs, ax
    pop rdi
    push qword 0x08
    push rdi
    retfq

%macro ISR 1
isr%1:
%if %1 != 8 && !(%1 >= 10 && %1 <= 14) && %1 != 17 && %1 != 21 && %1 != 29 && %1 != 30
    push qword 0
%endif
    push qword %1
    jmp isr_common
%endmacro
%assign i 0
%rep 48
ISR i
%assign i i+1
%endrep
isr_common:
    push rax
    push rcx
    push rdx
    push rbx
    push rbp
    push rsi
    push rdi
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov rdi, rsp
    mov rbp, rsp
    and rsp, -16
    cld
    call isr_handler
    mov rsp, rbp
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    pop rdx
    pop rcx
    pop rax
    add rsp, 16
    iretq

section .rodata
global isr_table
isr_table:
%assign i 0
%rep 48
    dq isr %+ i
%assign i i+1
%endrep

section .bss
alignb 16
    resb 524288
stack_top:
section .note.GNU-stack noalloc noexec nowrite
