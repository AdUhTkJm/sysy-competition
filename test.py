#!/bin/python3
import argparse;
import subprocess as proc;
import os;

parser = argparse.ArgumentParser()
parser.add_argument("-g", "--gdb", action="store_true")
parser.add_argument("-V", "--valgrind", action="store_true")
parser.add_argument("-v", "--verbose", action="store_true")
parser.add_argument("-r", "--dump-mid-ir", action="store_true")
parser.add_argument("--arm", action="store_true")
parser.add_argument("-a", "--test-all", action="store_true")
parser.add_argument("-t", "--test", type=str)

args = parser.parse_args()

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
  command = ["bin/sysc", f"test/{args.test}"]
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
  
  proc.run(command, check=True)
