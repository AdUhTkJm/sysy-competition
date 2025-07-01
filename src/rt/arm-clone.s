; R"(
# x0: Function pointer
# x1: Stack top
instantiate_worker:
  sub sp, sp, #16
  stp x19, x20, [sp, #0]
  dmb ish
  # Syscall 220:
  #   clone(flags, stack_top, parent_tid_ptr, child_tid_ptr, tls)
  mov x19, x0
  mov x20, x1

  # CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM
  mov x0, #3840
  movk x0, #5, lsl 16
  mov x2, #0
  mov x3, #0
  mrs x4, tpidr_el0
  mov x8, #220
  svc #0

  # For parent process, tid != 0, so diretly returns.
  cbnz x0, 1f

  # For child process, call the function.
  mov sp, x20
  blr x19

  # Exit child process when the function completes.
  # Syscall 93:
  #   exit(value)
  mov x0, #0
  mov x8, #93
  svc #0

1:
  ldp x19, x20, [sp, #0]
  add sp, sp, #16
  ret
)"
