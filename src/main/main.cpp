#include "../parse/Parser.h"
#include "../parse/Sema.h"
#include "../codegen/CodeGen.h"
#include "../opt/Passes.h"
#include "../opt/LoopPasses.h"
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

  pm.addPass<sys::AtMostOnce>();
  pm.addPass<sys::Localize>(/*beforeFlattenCFG=*/ true);
  pm.addPass<sys::EarlyConstFold>();
  pm.addPass<sys::Pureness>();
  pm.addPass<sys::DCE>(/*elimBlocks=*/ false);

  // ===== Flattened CFG =====

  pm.addPass<sys::FlattenCFG>();
  pm.addPass<sys::GVN>();
  pm.addPass<sys::DCE>();
  pm.addPass<sys::Inline>(/*inlineThreshold=*/ 200);
  pm.addPass<sys::DCE>();
  pm.addPass<sys::Localize>(/*beforeFlattenCFG=*/ false);
  pm.addPass<sys::Globalize>();
  pm.addPass<sys::Mem2Reg>();
  pm.addPass<sys::DCE>();
  pm.addPass<sys::CanonicalizeLoop>();
  pm.addPass<sys::GVN>();
  pm.addPass<sys::Alias>();
  pm.addPass<sys::DAE>();
  pm.addPass<sys::DSE>();
  pm.addPass<sys::DLE>();
  pm.addPass<sys::LateConstFold>();
  pm.addPass<sys::StrengthReduct>();
  pm.addPass<sys::DCE>();
  pm.addPass<sys::GVN>();
  pm.addPass<sys::GCM>();
  // Note that Mem2Reg can only be executed once. 
  // That's why we need a late inline here.

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
  // Add a newline at the end.
  // Single-line comments cannot terminate with EOF.
  ss << ifs.rdbuf() << "\n";

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
  pm.setPrintAfter(opts.printAfter);
  
  initPipeline(pm);
  pm.run();
  return 0;
}
