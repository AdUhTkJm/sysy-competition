#include "Options.h"
#include <cstring>
#include <iostream>

using namespace sys;

#define PARSEOPT(str, field) \
  if (strcmp(argv[i], str) == 0) { \
    opts.field = true; \
    continue; \
  }

Options::Options() {
  noLink = false;
  dumpAST = false;
  dumpMidIR = false;
  o1 = false;
  arm = false;
  rv = false;
  verbose = false;
  stats = false;
  verify = false;
}

Options sys::parseArgs(int argc, char **argv) {
  Options opts;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-o") == 0) {
      opts.outputFile = argv[i + 1];
      i++;
      continue;
    }

    if (strcmp(argv[i], "--print-after") == 0) {
      opts.printAfter = argv[i + 1];
      i++;
      continue;
    }

    PARSEOPT("--dump-ast", dumpAST);
    PARSEOPT("--dump-mid-ir", dumpMidIR);
    PARSEOPT("--rv", rv);
    PARSEOPT("--arm", arm);
    PARSEOPT("-O1", o1);
    PARSEOPT("-S", noLink);
    PARSEOPT("-v", verbose);
    PARSEOPT("--stats", stats);
    PARSEOPT("--verify", verify);

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