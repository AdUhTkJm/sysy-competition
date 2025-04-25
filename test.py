#!/bin/python3
import argparse;
import subprocess as proc;
import os;

parser = argparse.ArgumentParser()
parser.add_argument("-g", "--gdb", action="store_true")
parser.add_argument("-V", "--valgrind", action="store_true")
parser.add_argument("-v", "--verbose", action="store_true")
parser.add_argument("-s", "--stats", action="store_true")
parser.add_argument("-r", "--dump-mid-ir", action="store_true")
parser.add_argument("--arm", action="store_true")
parser.add_argument("-a", "--test-all", action="store_true")
parser.add_argument("-n", "--no-execute", action="store_true")
parser.add_argument("-t", "--test", type=str)

args = parser.parse_args()

def run(full_file: str, no_exec: bool):
  file = os.path.splitext(full_file)[0]

  command = ["bin/sysc", f"test/{full_file}"]

  if args.gdb:
    command = ["gdb", "--args", *command]
  
  if args.valgrind:
    command = ["valgrind", *command]
  
  if args.dump_mid_ir:
    command.append("--dump-mid-ir")
  
  if args.arm:
    command.append("--arm")
  
  if args.verbose:
    command.append("-v")
  
  if args.stats:
    command.append("--stats")

  command.extend(["-o", f"temp/{file}.s"])
  
  # Invoke SysY compiler.
  proc.run(command, check=True)

  if no_exec:
    return;
  
  # Invoke gcc.
  gcc = "aarch64-linux-gnu-gcc" if args.arm else "riscv64-linux-gnu-gcc"
  proc.run([gcc, f"temp/{file}.s", "test/official/sylib.c", "-static", "-o", f"temp/{file}"], check=True)

  # Run the file.
  qemu = "qemu-aarch64-static" if args.arm else "qemu-riscv64-static"
  return proc.run([qemu, f"temp/{file}"])


proc.run(["make"], check=True)
if args.test_all:
  names = os.listdir("test/official")
  for file in names:
    if not file.endswith(".sy"):
      continue;

    basename = file[:-3] # Remove ".sy"
    output = basename + ".out"
    input = basename + ".in"
    # TODO

if args.test:
  result = run(args.test, args.no_execute)
  if not args.no_execute:
    print(result.returncode)
