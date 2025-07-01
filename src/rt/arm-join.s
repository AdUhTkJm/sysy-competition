; R"(
# Arg 0: Lock address
spinlock_wait:
1:
  ldaxr w1, [x0]
  cbz w1, 2f
  clrex
  wfe
  b 1b
2:
  dmb ish
  ret
)"
