#!/bin/zsh

# I have multiple LLVM's installed:
# one for Clang IR, one for mainstream, one from `apt install`.
# Only the mainstream one has `llvm-mca` with AArch64 and RISC-V enabled.
# Hence this script, used as an alias of `llvm-mca` from mainstream though it's not on path.

~/llvm/llvm-project/build/bin/llvm-mca $@ -march=riscv64 -mcpu=xiangshan-nanhu
