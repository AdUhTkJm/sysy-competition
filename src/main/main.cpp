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
#include "../pre-opt/PreLoopPasses.h"
#include "../pre-opt/PreAnalysis.h"
#include "../arm/ArmPasses.h"
#include "../rv/RvPasses.h"
#include "../utils/smt/SMT.h"

using namespace smt;

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
  pm.addPass<sys::RaiseToFor>();
  pm.addPass<sys::DCE>(/*elimBlocks=*/ false);
  pm.addPass<sys::ArrayAccess>();
  pm.addPass<sys::Lower>();

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

void removeDuplicates(std::vector<Atomic>& clause) {
  std::sort(clause.begin(), clause.end());
  auto last = std::unique(clause.begin(), clause.end());
  clause.erase(last, clause.end());
}

void sat() {
  Solver solver;
  std::string line;
  std::getline(std::cin, line);
  while (line[0] == 'c')
    std::getline(std::cin, line);

  std::istringstream headerStream(line);
  int n, m;
  std::string dummy;
  headerStream >> dummy >> dummy >> m >> n;
  solver.init(m);

  for (int i = 0; i < n; ++i) {
    std::getline(std::cin, line);

    std::vector<Atomic> clause;
    std::istringstream lineStream(line);
    int lit;

    while (lineStream >> lit) {
      if (lit == 0)
        break;
      auto var = std::abs(lit) - 1;
      clause.push_back((Atomic) (lit < 0 ? var * 2 + 1 : var * 2));
    }

    removeDuplicates(clause);
    solver.addClause(clause);
  }
  std::vector<signed char> assignments;
  bool succ = solver.solve(assignments);
  if (!succ) {
    std::cout << "unsat\n";
    return;
  }

  std::cout << "sat\n";
  for (int i = 0; i < m; i++)
    std::cout << (i + 1) << " = " << (assignments[i] ? "true" : "false") << "\n";
}

void bv(const sys::Options &opts) {
  BvSolver solver(opts);
  BvExprContext ctx;
  auto _1 = ctx.create(BvExpr::Const, 7);
  auto _2 = ctx.create(BvExpr::Const, 4);
  auto _3 = ctx.create(BvExpr::Mul, _1, _2);

  auto _4 = ctx.create(BvExpr::Var, "x");
  auto _5 = ctx.create(BvExpr::Const, 2);
  auto _6 = ctx.create(BvExpr::Mul, _4, _5);
  auto _7 = ctx.create(BvExpr::Eq, _3, _6);

  bool succ = solver.infer(_7);
  if (succ) {
    std::cout << "sat\n";
    std::cout << "x = " << solver.extract("x") << "\n";
  } else std::cout << "unsat\n";
}

int main(int argc, char **argv) {
  opts = sys::parseArgs(argc, argv);

  // Test for submodules: bitvector SMT solver, and CDCL SAT solver.
  if (opts.bv) {
    bv(opts);
    return 0;
  }
  if (opts.sat) {
    sat();
    return 0;
  }

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
