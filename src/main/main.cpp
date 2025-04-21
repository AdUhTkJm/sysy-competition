#include "../parse/Parser.h"
#include "Options.h"
#include <fstream>
#include <sstream>

int main(int argc, char **argv) {
  auto opts = sys::parseArgs(argc, argv);

  // Read input file.
  std::ifstream ifs(opts.inputFile);
  if (!ifs) {
    std::cerr << "cannot open file\n";
    return 1;
  }

  std::stringstream ss;
  ss << ifs.rdbuf();

  sys::Parser parser(ss.str());
  sys::ASTNode *node = parser.parse();
  return 0;
}
