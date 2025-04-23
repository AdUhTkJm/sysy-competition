#include "Options.h"
#include <cstring>
#include <iostream>

using namespace sys;

Options::Options() {
  noLink = false;
  dumpAST = false;
  dumpMidIR = false;
  o1 = false;
  arm = false;
  rv = false;
}

Options sys::parseArgs(int argc, char **argv) {
  Options opts;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-o") == 0) {
      opts.outputFile = argv[i + 1];
      i++;
      continue;
    }

    if (strcmp(argv[i], "--dump-ast") == 0) {
      opts.dumpAST = true;
      continue;
    }

    if (strcmp(argv[i], "--dump-mid-ir") == 0) {
      opts.dumpMidIR = true;
      continue;
    }

    if (strcmp(argv[i], "--rv") == 0) {
      opts.rv = true;
      continue;
    }

    if (strcmp(argv[i], "--arm") == 0) {
      opts.arm = true;
      continue;
    }

    if (strcmp(argv[i], "-O1") == 0) {
      opts.o1 = true;
      continue;
    }

    if (strcmp(argv[i], "-S") == 0) {
      opts.noLink = true;
      continue;
    }

    if (opts.inputFile != "") {
      std::cerr << "error: multiple inputs\n";
      exit(1);
    }

    opts.inputFile = argv[i];
  }

  if (opts.rv && opts.arm) {
    std::cerr << "error: multiple target\n";
    exit(1);
  }

  // Default to RISC-V.
  if (!opts.rv && !opts.arm)
    opts.rv = true;

  return opts;
}