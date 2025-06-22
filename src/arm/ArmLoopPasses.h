#ifndef ARM_LOOP_PASSES_H
#define ARM_LOOP_PASSES_H

#include "../opt/Pass.h"
#include "../codegen/CodeGen.h"
#include "../codegen/Ops.h"
#include "../codegen/Attrs.h"
#include "ArmOps.h"
#include "ArmAttrs.h"
#include "../opt/LoopPasses.h"

namespace sys::arm {

// Convert SCEV addresses to post-increment.
class PostIncr : public Pass {
  void runImpl(LoopInfo *info);
public:
  PostIncr(ModuleOp *module): Pass(module) {}

  std::string name() override { return "arm-post-incr"; };
  std::map<std::string, int> stats() override { return {}; }
  void run() override;
};

}

#endif
