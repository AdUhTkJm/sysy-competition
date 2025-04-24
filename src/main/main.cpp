#include "../parse/Parser.h"
#include "../parse/Sema.h"
#include "../codegen/CodeGen.h"
#include "../opt/Passes.h"
#include "../arm/ArmPasses.h"
#include "../rv/RvPasses.h"
#include "Options.h"
#include <fstream>
#include <sstream>

void initArmPipeline(sys::PassManager &pm) {
  using namespace sys::arm;

  pm.addPass<Lower>();
}

void initRvPipeline(sys::PassManager &pm) {
  using namespace sys::rv;

  pm.addPass<Lower>();
  pm.addPass<InstCombine>();
}

void initPipeline(sys::PassManager &pm, const sys::Options &opts) {
  pm.addPass<sys::MoveAlloca>();

  // ===== Structured control flow =====

  pm.addPass<sys::Pureness>();
  pm.addPass<sys::DCE>();

  // ===== Flattened CFG =====

  pm.addPass<sys::FlattenCFG>();
  pm.addPass<sys::Mem2Reg>();
  // Update <impure> for instructions introduced by previous passes.
  pm.addPass<sys::Pureness>();
  pm.addPass<sys::DCE>();

  if (opts.arm)
    initArmPipeline(pm);

  if (opts.rv)
    initRvPipeline(pm);
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
  if (opts.dumpMidIR)
    cg.getModule()->dump(std::cerr);

  sys::PassManager pm(cg.getModule());
  pm.setVerbose(opts.verbose);
  pm.setPrintStats(opts.stats);
  
  initPipeline(pm, opts);
  pm.run();
  pm.getModule()->dump(std::cerr);
  return 0;
}
