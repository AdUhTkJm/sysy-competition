#ifndef OPTIONS_H
#define OPTIONS_H

#include <string>

namespace sys {

struct Options {
  using option = unsigned char;

  struct {
    option dumpAST : 1;
    option noLink : 1;
    option dumpMidIR : 1;
    option o1 : 1;
    option arm : 1;
    option rv : 1;
    option verbose : 1;
    option stats : 1;
    option verify : 1;
    option bv : 1;
    option sat : 1;
  };

  std::string inputFile;
  std::string outputFile;
  std::string printAfter;
  std::string compareWith;
  std::string simulateInput;
  
  Options();
};

Options parseArgs(int argc, char **argv);

}

#endif
