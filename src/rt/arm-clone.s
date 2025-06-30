; x0: Function pointer
; x1: Stack top
clone_worker:
  ; Syscall 220:
  ;   clone(flags, stack_top, parent_tid_ptr, child_tid_ptr, tls)
  mov x19, x0

  ; CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_THREAD | CLONE_SIGHAND
  mov x0, 0x10F00
  mov x2, xzr
  mov x3, xzr
  mov x4, xzr
  svc #0

  cbnz x0, __parent

  ; 
.L__parent: