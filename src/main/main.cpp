#include "../parse/Parser.h"
#include "../parse/Sema.h"
#include "../codegen/CodeGen.h"
#include "../opt/Passes.h"
#include "../arm/ArmPasses.h"
#include "../rv/RvPasses.h"
#include "Options.h"
#include <fstream>
#include <sstream>

sys::Options opts;

void initArmPipeline(sys::PassManager &pm) {
  using namespace sys::arm;

  pm.addPass<Lower>();
}

void initRvPipeline(sys::PassManager &pm) {
  using namespace sys::rv;

  pm.addPass<Lower>();
  pm.addPass<InstCombine>();
  pm.addPass<RvDCE>();
  pm.addPass<RegAlloc>();
  pm.addPass<Dump>(opts.outputFile);
}

void initPipeline(sys::PassManager &pm) {
  pm.addPass<sys::MoveAlloca>();
  pm.addPass<sys::ImplicitReturn>();

  // ===== Structured control flow =====

  pm.addPass<sys::StrengthReduct>();
  pm.addPass<sys::Pureness>();
  pm.addPass<sys::DCE>();

  // ===== Flattened CFG =====

  pm.addPass<sys::FlattenCFG>();
  pm.addPass<sys::Mem2Reg>();
  pm.addPass<sys::DCE>();

  if (opts.arm)
    initArmPipeline(pm);

  if (opts.rv)
    initRvPipeline(pm);
}

int main(int argc, char **argv) {
  opts = sys::parseArgs(argc, argv);

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
  if (opts.dumpMidIR)
    cg.getModule()->dump(std::cerr);

  sys::PassManager pm(cg.getModule());
  pm.setVerbose(opts.verbose);
  pm.setPrintStats(opts.stats);
  
  initPipeline(pm);
  pm.run();
  return 0;
}
