#!/bin/python3
import argparse;
import subprocess as proc;

parser = argparse.ArgumentParser()
parser.add_argument("-g", "--gdb", action="store_true")
parser.add_argument("-r", "--dump-mid-ir", action="store_true")
parser.add_argument("-t", "--test", type=str)

args = parser.parse_args()

proc.run(["make"], check=True)
if args.test:
  command = ["bin/sysc", f"test/{args.test}"]
  if args.gdb:
    command = ["gdb", "--args", *command]
  if args.dump_mid_ir:
    command.append("--dump-mid-ir")
  proc.run(command, check=True)
