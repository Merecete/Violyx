# Start of a C program on Violyx, the system call and the helpers that need the processor directly.
  .text
  .globl _start
_start:
  and $-16, %rsp
  call __libc_start
1:
  jmp 1b

# long vx_sys(long n, long a, long b, long c)
  .globl vx_sys
vx_sys:
  mov %rdi, %rax
  mov %rsi, %rdi
  mov %rdx, %rsi
  mov %rcx, %rdx
  int $48
  ret
