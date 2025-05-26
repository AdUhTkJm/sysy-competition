#ifndef LOWER_PASSES_H
#define LOWER_PASSES_H

#include "Pass.h"
#include "../codegen/Attrs.h"
#include "../codegen/Ops.h"

namespace sys {

class FlattenCFG : public Pass {
public:
  FlattenCFG(ModuleOp *module): Pass(module) {}
  
  std::string name() override { return "flatten-cfg"; };
  std::map<std::string, int> stats() override { return {}; };
  void run() override;
};

// A weak scheduler that only works on basic blocks.
// This can't be in backend, because backends require that writereg-call-readreg must stay together.
class InstSchedule : public Pass {
  void runImpl(BasicBlock *bb);
public:
  InstSchedule(ModuleOp *module): Pass(module) {}

  std::string name() override { return "inst-schedule"; };
  std::map<std::string, int> stats() override { return {}; }
  void run() override;
};

}

#endif
