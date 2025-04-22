#include "../parse/Parser.h"
#include "../parse/Sema.h"
#include "../codegen/CodeGen.h"
#include "../opt/Passes.h"
#include "Options.h"
#include <fstream>
#include <sstream>

void initPipeline(sys::PassManager &pm) {
  pm.addPass<sys::FlattenCFG>();
}

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

  sys::TypeContext ctx;

  sys::Parser parser(ss.str(), ctx);
  sys::ASTNode *node = parser.parse();
  sys::Sema sema(node, ctx);

  sys::CodeGen cg(node);
  cg.getModule()->dump(std::cerr);

  sys::PassManager pm(cg.getModule());
  initPipeline(pm);
  pm.run();
  pm.getModule()->dump(std::cerr);
  return 0;
}
