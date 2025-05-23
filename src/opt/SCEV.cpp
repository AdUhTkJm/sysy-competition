#include "LoopPasses.h"
#include "../utils/Matcher.h"

using namespace sys;

std::map<std::string, int> SCEV::stats() {
  return {
    { "expanded", expanded }
  };
}

static Rule constIncr("(add (x 'a))");

void SCEV::runImpl(LoopInfo *info) {
  if (info->getLatches().size() > 1)
    return;

  auto header = info->getHeader();
  auto latch = info->getLatch();

  // Inspect phis to find the amount by which something increases.
  auto phis = header->getPhis();
  for (auto phi : phis) {
    auto latchval = Op::getPhiFrom(phi, latch);
    // Try to match (add (x 'a)).
    if (constIncr.match(latchval, { { "x", phi } })) {
      auto v = constIncr.extract("'a");
      phi->add<IncreaseAttr>(V(v));
    }
  }

  // TODO
}

void SCEV::run() {
  LoopAnalysis analysis(module);
  analysis.run();
  auto forests = analysis.getResult();

  auto funcs = collectFuncs();
  for (auto func : funcs) {
    const auto &forest = forests[func];

    for (auto loop : forest.getLoops())
      runImpl(loop);
  }

  // Erase all IncreaseAttr from phis.
  // Phi rely on subscript to find FromAttr,
  // so no other attributes are allowed for them.
  for (auto func : funcs) {
    auto region = func->getRegion();
    for (auto bb : region->getBlocks()) {
      auto phis = bb->getPhis();

      for (auto phi : phis)
        phi->remove<IncreaseAttr>();
    }
  }
}
