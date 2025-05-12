#!/bin/python3
import io
import random as r
import collections as c

Type = c.namedtuple("Type", ["base", "size"])

class Symbols:
  def __init__(self):
    self.arrs: dict[str, Type] = {} # Maps name to size.
    self.const_arrs: dict[str, Type] = {}
    self.vars: dict[str, Type] = {}
    self.const_vars: dict[str, Type] = {}

  def reset(self):
    self.arrs = {}
    self.const_arrs = {}
    self.vars = {}
    self.const_vars = {}


table = Symbols()
f: io.TextIOWrapper = None
cnt = 0

def rand() -> bool:
  return r.choice([True, False])

def randx(x: float):
  return r.random() < x

def basety() -> str:
  return r.choice(["int", "float"])

def name() -> str:
  global cnt
  cnt += 1
  return f"v{cnt}"

def vars(const: bool) -> set[str]:
  return table.vars.keys() if not const else table.const_vars.keys()

def arrs(const: bool) -> set[str]:
  return table.arrs.keys() if not const else table.const_arrs.keys()

def gen_var(const: bool):
  var = r.choice(list(vars(const)))
  f.write(var)

def gen_arr_access(const: bool):
  arr = r.choice(list(arrs(const)))
  size = table.arrs[arr].size
  f.write(f"{arr}[{r.randint(0, size - 1)}]")

def gen_expr_helper(const: bool, remain: int, ops: str):
  if remain == 0:
    if randx(0.4) and len(arrs(const)):
      gen_arr_access(const)
    elif randx(0.571428) and len(vars(const)):
      gen_var(const)
    else:
      f.write(str(r.randint(-17, 60)))
    return
  
  if randx(0.1):
    op = r.choice(["!", "-"]) # Unary
    par = randx(0.2)

    f.write(op)
    if par:
      f.write('(')
    gen_expr_helper(const, remain - 1, ops)
    if par:
      f.write(')')
    return; 

  op = r.choice(ops)
  parl = randx(0.2)
  parr = randx(0.2)
  if parl:
    f.write('(')
  gen_expr_helper(const, remain - 1, ops)
  if parl:
    f.write(')')
  f.write(f" {op} ")
  if parr:
    f.write('(')
  gen_expr_helper(const, remain - 1, ops)
  if parr:
    f.write(')')


def gen_expr(const: bool, **kwargs):
  depth = r.randint(1, 5) if kwargs.get("depth") is None else kwargs["depth"]
  ops = ["+", "-", "*", "/", "%"] if kwargs.get("compare") is None else \
        ["+", "-", "*", "/", "%", "!=", "==", ">=", "<=", "<", ">", "&&", "||"]
  gen_expr_helper(const, depth, ops)

def gen_arr(const: bool):
  ty = basety()
  n = name()
  size = r.randint(1, 256)
  f.write(f"{ty} {n}[{size}]")
  if rand():
    f.write(" = {")
    gen_expr(const, depth=r.randint(1, 2))
    for _ in range(1, r.randint(1, size)):
      f.write(", ")
      gen_expr(const, depth=r.randint(1, 2))
    f.write("}")
  
  f.write(";\n")
  table.arrs[n] = Type(ty, size)

def gen_v(const: bool):
  ty = basety()
  n = name()
  f.write(f"{ty} {n}")
  if rand():
    f.write(" = ")
    gen_expr(const)
  
  f.write(";\n")
  table.vars[n] = Type(ty, None)

def gen_var_assign():
  gen_var(False)
  f.write(" = ")
  gen_expr(False)
  f.write(";\n")

def gen_arr_assign():
  gen_arr_access(False)
  f.write(" = ")
  gen_expr(False)
  f.write(";\n")

def gen_body():
  for _ in range(r.randint(12, 60)):
    f.write("  ")
    x = r.random()
    if x < 0.05:
      gen_arr(False)
    elif x < 0.2:
      gen_v(False)
    elif x < 0.7 and len(vars(False)):
      gen_var_assign()
    elif x < 0.8:
      f.write("putint(")
      gen_expr(False)
      f.write(");\n")
    elif len(arrs(False)):
      gen_arr_assign()
    else:
      f.write(";\n") # Empty statement

def gen_program():
  global table

  for _ in range(r.randint(1, 3)):
    r.choice([gen_arr, gen_v])(True)


  f.write("int main() {\n")
  gen_body()
  f.write("  return 0;\n}")

if __name__ == "__main__":
  for i in range(0, 3):
    cnt = 0
    table.reset()
    
    f = open(f"test/fuzz/{i}.sy", "w")
    gen_program()
    f.close()
