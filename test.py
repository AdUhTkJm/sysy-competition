#!/bin/python3
import argparse;
import subprocess as proc;
import os;
import hashlib;
import pickle;
from pathlib import Path;

parser = argparse.ArgumentParser()
parser.add_argument("-g", "--gdb", action="store_true")
parser.add_argument("-V", "--valgrind", action="store_true")
parser.add_argument("-v", "--verbose", action="store_true")
parser.add_argument("-s", "--stats", action="store_true")
parser.add_argument("-r", "--dump-mid-ir", action="store_true")
parser.add_argument("--arm", action="store_true")
parser.add_argument("-a", "--test-all", action="store_true")
parser.add_argument("-n", "--no-execute", action="store_true")
parser.add_argument("--asm", type=str)
parser.add_argument("-t", "--test", type=str)

args = parser.parse_args()

SRC_DIR = Path("src")
BUILD_DIR = Path("build")
FINAL_BINARY = BUILD_DIR / "sysc"
COMPILER = "clang++"
AR = "ar"
CFLAGS = ["-c", "-std=c++17", "-g"]
LDFLAGS = []
CACHE_FILE = BUILD_DIR / ".build_cache.pkl"

def hash_file(path):
  h = hashlib.sha256()
  with open(path, 'rb') as f:
    h.update(f.read())
  return h.hexdigest()

def find_files():
  cpp_files = []
  h_files = []
  for path in SRC_DIR.rglob("*"):
    if path.suffix == ".cpp":
      cpp_files.append(path)
    elif path.suffix == ".h":
      h_files.append(path)
  return cpp_files, h_files

def load_cache():
  if CACHE_FILE.exists():
    with open(CACHE_FILE, "rb") as f:
      return pickle.load(f)
  return {}

def save_cache(cache):
  BUILD_DIR.mkdir(parents=True, exist_ok=True)
  with open(CACHE_FILE, "wb") as f:
    pickle.dump(cache, f)

def needs_recompile(src_path, obj_path, cache, dependencies):
  src_hash = hash_file(src_path)
  dep_hashes = { str(dep): hash_file(dep) for dep in dependencies }

  prev = cache.get(str(src_path))
  if not prev:
      return True
  if prev['src_hash'] != src_hash:
      return True
  if prev['dep_hashes'] != dep_hashes:
      return True
  if not obj_path.exists():
      return True
  return False

def get_includes(src_path):
  includes = []
  with open(src_path, "r") as f:
    for line in f:
      line = line.strip()
      if line.startswith("#include \""):
        header = line.split("\"")[1]
        include_path = src_path.parent / header
        if include_path.exists():
          includes.append(include_path.resolve())
  return includes

def compile_cpp(src_path, obj_path):
  obj_path.parent.mkdir(parents=True, exist_ok=True)
  print(f"Compiling {src_path} -> {obj_path}")
  proc.check_call([COMPILER] + CFLAGS + ["-o", str(obj_path), str(src_path)])

def archive_objects(obj_files, lib_path):
  if lib_path.exists():
      lib_path.unlink()
  print(f"Creating archive {lib_path}")
  proc.check_call([AR, "rcs", str(lib_path)] + [str(obj) for obj in obj_files])

def link_libraries(lib_files, output_binary):
  print(f"Linking {output_binary}")
  proc.check_call([COMPILER] + LDFLAGS + ["-o", str(output_binary)] + [str(lib) for lib in lib_files])

def build():
  cpp_files, _ = find_files()
  cache = load_cache()

  # Step 1: Compile .cpp to .o
  obj_files = []
  folder_changed = {}
  for cpp in cpp_files:
    rel_dir = cpp.relative_to(SRC_DIR).parent
    obj_dir = BUILD_DIR / rel_dir
    obj_path = obj_dir / (cpp.stem + ".o")

    dependencies = get_includes(cpp)
    if needs_recompile(cpp, obj_path, cache, dependencies):
      compile_cpp(cpp, obj_path)
      cache[str(cpp)] = {
        'src_hash': hash_file(cpp),
        'dep_hashes': {str(dep): hash_file(dep) for dep in dependencies},
      }
      folder_changed[rel_dir] = True
    obj_files.append((rel_dir, obj_path))

  # Step 2: Archive .o's in same folder into .a
  folder_objs = {}
  for rel_dir, obj in obj_files:
    folder_objs.setdefault(rel_dir, []).append(obj)

  lib_files = []
  for folder, objs in folder_objs.items():
    lib_path = BUILD_DIR / folder / (folder.name + ".a")
    need_archive = folder_changed.get(folder, False) or not lib_path.exists()
    if need_archive:
      archive_objects(objs, lib_path)
    else:
      print(f"Skipping archive {lib_path}, no changes")
    lib_files.append(lib_path)

  # Step 3: Link all .a's into final binary
  link_libraries(lib_files, FINAL_BINARY)

  save_cache(cache)


def run_asm(file: str):
  basename = os.path.splitext(os.path.basename(file))[0]

  # Invoke gcc.
  gcc = "aarch64-linux-gnu-gcc" if args.arm else "riscv64-linux-gnu-gcc"
  proc.run([gcc, file, "test/official/sylib.c", "-static", "-o", f"temp/{basename}"], check=True)

  # Run the file.
  qemu = "qemu-aarch64-static" if args.arm else "qemu-riscv64-static"
  return proc.run([qemu, f"temp/{basename}"])

def run(full_file: str, no_exec: bool):
  basename = os.path.splitext(os.path.basename(full_file))[0]

  command = [f"{BUILD_DIR}/sysc", f"test/{full_file}"]

  if args.gdb:
    command = ["gdb", "--args", *command]
    no_exec = True
  
  if args.valgrind:
    command = ["valgrind", *command]
    no_exec = True
  
  if args.dump_mid_ir:
    command.append("--dump-mid-ir")
  
  if args.arm:
    command.append("--arm")
  
  if args.verbose:
    command.append("-v")
  
  if args.stats:
    command.append("--stats")

  command.extend(["-o", f"temp/{basename}.s"])
  
  # Invoke SysY compiler.
  proc.run(command, check=True)

  if no_exec:
    return;
  return run_asm(f"temp/{basename}.s")

if args.asm:
  result = run_asm(args.asm)
  print("Return value: ", result.returncode)
  exit(0)

build()
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
    print("Return value: ", result.returncode)
