#ifndef OPTIONS_H
#define OPTIONS_H

#include <string>

namespace sys {

struct Options {
  using option = unsigned char;

  struct {
    option dumpAST : 1;
  };

  std::string inputFile;
  std::string outputFile;
};

Options parseArgs(int argc, char **argv);

}

#endif
