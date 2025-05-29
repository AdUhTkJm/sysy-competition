#include "Options.h"
#include <fstream>
#include <sstream>

#include "../parse/Parser.h"
#include "../parse/Sema.h"
#include "../codegen/CodeGen.h"
#include "../opt/PassManager.h"
#include "../opt/Passes.h"
#include "../opt/LoopPasses.h"
#include "../opt/CleanupPasses.h"
#include "../opt/LowerPasses.h"
#include "../opt/Analysis.h"
#include "../pre-opt/PrePasses.h"
// #include "../pre-opt/PreLoopPasses.h"
#include "../arm/ArmPasses.h"
#include "../rv/RvPasses.h"

sys::Options opts;

void initArmPipeline(sys::PassManager &pm) {
  using namespace sys::arm;

  pm.addPass<Lower>();
  pm.addPass<InstCombine>();
  pm.addPass<ArmDCE>();
  pm.addPass<RegAlloc>();
  pm.addPass<LateLegalize>();
  pm.addPass<Dump>(opts.outputFile);
}

void initRvPipeline(sys::PassManager &pm) {
  using namespace sys::rv;

  pm.addPass<Lower>();
  pm.addPass<StrengthReduct>();
  pm.addPass<InstCombine>();
  pm.addPass<RvDCE>();
  pm.addPass<RegAlloc>();
  pm.addPass<Dump>(opts.outputFile);
}

void initPipeline(sys::PassManager &pm) {
  pm.addPass<sys::MoveAlloca>();

  // ===== Structured control flow =====

  pm.addPass<sys::AtMostOnce>();
  pm.addPass<sys::Localize>(/*beforeFlattenCFG=*/ true);
  pm.addPass<sys::EarlyConstFold>(/*beforePureness=*/ true);
  pm.addPass<sys::Pureness>();
  pm.addPass<sys::EarlyConstFold>(/*beforePureness=*/ false);
  pm.addPass<sys::TCO>();
  pm.addPass<sys::Remerge>();
  // pm.addPass<sys::RaiseToFor>();
  pm.addPass<sys::DCE>(/*elimBlocks=*/ false);
  // pm.addPass<sys::Lower>();

  // ===== Flattened CFG =====

  pm.addPass<sys::FlattenCFG>();
  pm.addPass<sys::GVN>();
  pm.addPass<sys::DCE>();
  pm.addPass<sys::Inline>(/*inlineThreshold=*/ 200);
  pm.addPass<sys::DCE>();
  pm.addPass<sys::Localize>(/*beforeFlattenCFG=*/ false);
  pm.addPass<sys::Globalize>();
  pm.addPass<sys::Mem2Reg>();
  pm.addPass<sys::Alias>();
  pm.addPass<sys::RegularFold>();
  pm.addPass<sys::DCE>();
  pm.addPass<sys::DAE>();
  pm.addPass<sys::GVN>();
  pm.addPass<sys::CanonicalizeLoop>(/*lcssa=*/ true);
  pm.addPass<sys::LoopRotate>();
  pm.addPass<sys::CanonicalizeLoop>(/*lcssa=*/ false);
  pm.addPass<sys::LICM>();
  pm.addPass<sys::ConstLoopUnroll>();
  pm.addPass<sys::SCEV>();
  pm.addPass<sys::GVN>();
  pm.addPass<sys::RegularFold>();
  pm.addPass<sys::DCE>();
  pm.addPass<sys::GVN>();
  pm.addPass<sys::SimplifyCFG>();
  pm.addPass<sys::Alias>();
  pm.addPass<sys::DAE>();
  pm.addPass<sys::DSE>();
  pm.addPass<sys::DLE>();
  pm.addPass<sys::Select>();
  pm.addPass<sys::RegularFold>();
  pm.addPass<sys::DCE>();
  pm.addPass<sys::GCM>();
  pm.addPass<sys::GVN>();
  pm.addPass<sys::AggressiveDCE>();

  // ===== Late Inline =====

  pm.addPass<sys::LateInline>(/*threshold=*/ 200);
  pm.addPass<sys::RegularFold>();
  pm.addPass<sys::GVN>();
  pm.addPass<sys::Alias>();
  pm.addPass<sys::DSE>();
  pm.addPass<sys::DLE>();
  pm.addPass<sys::DCE>();
  pm.addPass<sys::InlineStore>();
  pm.addPass<sys::GCM>();
  pm.addPass<sys::GVN>();
  pm.addPass<sys::RegularFold>();
  pm.addPass<sys::AggressiveDCE>();
  pm.addPass<sys::SimplifyCFG>();
  pm.addPass<sys::InstSchedule>();
  pm.addPass<sys::Verify>();

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
  delete node;

  sys::ModuleOp *module = cg.getModule();
  if (opts.dumpMidIR)
    std::cerr << module;

  sys::PassManager pm(module, opts);
  
  initPipeline(pm);
  
  pm.run();
  return 0;
}
