#include "Options.h"
#include <cstring>
#include <iostream>

using namespace sys;

Options sys::parseArgs(int argc, char **argv) {
  Options opts;

  for (int i = 1; i < argc;) {
    if (strcmp(argv[i], "-o") == 0) {
      opts.outputFile = argv[i + 1];
      i += 2;
      continue;
    }

    if (strcmp(argv[i], "--dump-ast") == 0) {
      opts.dumpAST = true;
      continue;
    }

    if (opts.inputFile != "") {
      std::cerr << "error: multiple inputs";
      exit(1);
    }

    opts.inputFile = argv[i];
    i++;
  }

  return opts;
}