# Mathematics with the floating point unit (the same on every system)
  .text
  .globl sqrt
sqrt:
  sqrtsd %xmm0, %xmm0
  ret
  .globl sqrtf
sqrtf:
  sqrtss %xmm0, %xmm0
  ret
# x87 helpers: argument and result in %xmm0
  .globl __x87_sin
__x87_sin:
  movsd %xmm0, -8(%rsp)
  fldl -8(%rsp)
  fsin
  fstpl -8(%rsp)
  movsd -8(%rsp), %xmm0
  ret
  .globl __x87_cos
__x87_cos:
  movsd %xmm0, -8(%rsp)
  fldl -8(%rsp)
  fcos
  fstpl -8(%rsp)
  movsd -8(%rsp), %xmm0
  ret
# atan2(y, x): y in %xmm0, x in %xmm1
  .globl atan2
atan2:
  movsd %xmm0, -8(%rsp)
  movsd %xmm1, -16(%rsp)
  fldl -8(%rsp)
  fldl -16(%rsp)
  fpatan
  fstpl -8(%rsp)
  movsd -8(%rsp), %xmm0
  ret
# log2(x) * y: x in %xmm0, y in %xmm1
  .globl __x87_ylog2x
__x87_ylog2x:
  movsd %xmm1, -8(%rsp)
  movsd %xmm0, -16(%rsp)
  fldl -8(%rsp)
  fldl -16(%rsp)
  fyl2x
  fstpl -8(%rsp)
  movsd -8(%rsp), %xmm0
  ret
# 2^x
  .globl __x87_exp2
__x87_exp2:
  movsd %xmm0, -8(%rsp)
  fldl -8(%rsp)
  fld %st(0)
  frndint
  fxch %st(1)
  fsub %st(1), %st
  f2xm1
  fld1
  faddp
  fscale
  fstp %st(1)
  fstpl -8(%rsp)
  movsd -8(%rsp), %xmm0
  ret
