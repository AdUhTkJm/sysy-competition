#ifndef PRE_ANALYSIS
#define PRE_ANALYSIS

#include "../opt/Pass.h"
#include "../codegen/Ops.h"
#include "../codegen/Attrs.h"

namespace sys {

// Marks addresses, loads and stores with `SubscriptAttr`.
class ArrayAccess : public Pass {
  // Takes all induction variables outside the current loop,
  // including that of the loop we're inspecting.
  // (In other words, outer.size() >= 1.)
  void runImpl(Op *loop, std::vector<Op*> outer);
public:
  ArrayAccess(ModuleOp *module): Pass(module) {}
    
  std::string name() override { return "array-access"; };
  std::map<std::string, int> stats() override { return {}; };
  void run() override;
};

}

#endif
