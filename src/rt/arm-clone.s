; R"(
# x0: Function pointer
# x1: Stack top
instantiate_worker:
  sub sp, sp, #8
  str x19, [sp, #0]
  # Syscall 220:
  #   clone(flags, stack_top, parent_tid_ptr, child_tid_ptr, tls)
  mov x19, x0

  # CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_THREAD | CLONE_SIGHAND
  mov x0, #0xF00
  movk x0, #1, lsl 16
  mov x2, xzr
  mov x3, xzr
  mov x4, xzr
  svc #0

  # For parent process, tid != 0, so diretly returns.
  cbnz x0, 1f

  # For child process, call the function.
  mov x0, x1
  blr x19

  # Exit child process when the function completes.
  # Syscall 93:
  #   exit(value)
  mov x0, #0
  mov x8, #93
  svc #0

1:
  ldr x19, [sp, #0]
  add sp, sp, #8
  ret
)"
