; R"(
# Arg 0: Lock address
futex_wait:
1:
  ldr w1, [x0]
  cmp w1, #0
  bne 2f

  # Syscall 98:
  #   futex(uaddr, futex_op = FUTEX_WAIT (0), val, timeout_ptr, uaddr2, val3)
  mov x1, #0
  mov x2, xzr
  mov x3, xzr
  mov x4, xzr
  mov x5, xzr
  mov x8, #98
  svc #0
  b 1b

2:
  ret

# Arg 0: Lock address
futex_wake:
  mov x1, #1
  # Wake up 1 thread
  mov x2, #1
  mov x3, xzr
  mov x4, xzr
  mov x5, xzr
  mov x8, #98
  svc #0
  ret
)"
