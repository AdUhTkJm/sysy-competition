#include "Options.h"
#include <cstring>
#include <iostream>

using namespace sys;

Options::Options() {
  noLink = false;
  dumpAST = false;
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

    if (strcmp(argv[i], "-S") == 0) {
      opts.noLink = true;
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