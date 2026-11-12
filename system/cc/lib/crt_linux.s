# Start of a C program on Linux: only for testing the compiler on the build computer.
  .text
  .globl _start
_start:
  and $-16, %rsp
  call __libc_start
1:
  jmp 1b

# long __syscall(long n, long a, long b, long c)
  .globl __syscall
__syscall:
  mov %rdi, %rax
  mov %rsi, %rdi
  mov %rdx, %rsi
  mov %rcx, %rdx
  syscall
  ret
