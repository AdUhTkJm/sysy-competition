#include "LoopPasses.h"

using namespace sys;

void LoopRotate::runImpl(LoopInfo *info) {
  if (info->getExits().size() > 1)
    return;

}

void LoopRotate::run() {
  // We know we've built a preheader during canonicalization.
  LoopAnalysis analysis(module);
  analysis.run();
  auto forests = analysis.getResult();

  auto funcs = collectFuncs();
  for (auto func : funcs) {
    const auto &forest = forests[func];
    for (auto loop : forest.getLoops())
      runImpl(loop);
  }
}
