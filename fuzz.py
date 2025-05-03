#!/bin/python3
import tempfile;
import os;
import subprocess as proc;
import random;

operators = [
  '+', '-', '*', '/', '%'
]
comparisons = [
  '<', '>', '=='
]
error_cnt = 0

def run(file: str, input: bytes):
  basename = os.path.splitext(os.path.basename(file))[0]
  try:
    proc.run(["build/sysc", file, "-o", f"temp/{basename}.s"], check=True, timeout=5)
  except:
    print("Compiler internal error.")
    with open(f"temp/bad_program.txt", "w") as f:
      f.write(open(file, "r").read())
    exit(1)
  
  gcc = "riscv64-linux-gnu-gcc"
  proc.run([gcc, f"temp/{basename}.s", "test/official/sylib.c", "-static", "-o", f"temp/{basename}"])

  # Run the file.
  qemu = "qemu-riscv64-static"
  try:
    return proc.run([qemu, f"temp/{basename}"], input=input, stdout=proc.PIPE, timeout=5)
  except:
    print("Program timeout.")
    with open(f"temp/bad_program.txt", "w") as f:
      f.write(open(file, "r").read())
    exit(1)

def run_actual(file: str, input: bytes):
  proc.run(["clang", file, "-o", f"{file}_clang"])

  return proc.run([f"{file}_clang"], stdout=proc.PIPE, input=input)

def fuzz_arithmetic_fold(dir: str):
  global error_cnt

  testcases = []
  for i in range(0, 500):
    c1 = random.randint(-2000, 10000)
    c2 = random.randint(-2000, 10000)
    op = random.choice(operators)
    comp = random.choice(comparisons)
    testcases.append(f"x {op} {c1} {comp} {c2}")
    testcases.append(f"{c1} {op} x {comp} {c2}")

  sy = os.path.join(dir, "file.sy")
  c = os.path.join(dir, "file.c")
  with open(sy, "w") as f:
    f.write("int main() {\n  int x = getint();\n  ")
    f.write('\n  '.join([f"putint({x}); putch(10);" for x in testcases]))
    f.write("\n}\n")

  with open(c, "w") as f:
    f.write("#include <stdio.h>\n")
    f.write('int main() {\n  int x; scanf("%d", &x);\n  ')
    f.write('\n  '.join([f'printf("%d\\n", {x});' for x in testcases]))
    f.write("\n}\n")

  data = (str(random.randint(-10000, 10000)) + "\n").encode('utf-8')
  expect_out = run_actual(c, input=data).stdout.decode('utf-8')
  actual_out = run(sy, input=data).stdout.decode('utf-8')
  if actual_out != expect_out:
    print(f"Error! Current error count: {error_cnt}")
    with open(f"temp/{error_cnt}_expected.txt", "w") as f:
      f.write(expect_out)
    with open(f"temp/{error_cnt}_actual.txt", "w") as f:
      f.write(actual_out)
    with open(f"temp/{error_cnt}_program.txt", "w") as f:
      f.write('\n'.join(testcases))
    error_cnt += 1

with tempfile.TemporaryDirectory() as dir:
  for i in range(0, 20):
    fuzz_arithmetic_fold(dir)
