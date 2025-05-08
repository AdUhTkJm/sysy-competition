#include "LoopPasses.h"

using namespace sys;

std::map<std::string, int> LoopUnroll::stats() {
  return {
    { "unrolled", unrolled }
  };
}

void LoopUnroll::runImpl(LoopInfo *loop) {
  if (!loop->getInduction())
    return;

  auto header = loop->getHeader();
  auto preheader = loop->getPreheader();
  auto latch = loop->getLatch();
  auto phis = header->getPhis();
  // Ensure that every phi at header is either from preheader or from the latch.
  for (auto phi : phis) {
    const auto &ops = phi->getOperands();
    const auto &attrs = phi->getAttrs();
    if (ops.size() != 2)
      return;
    auto bb1 = cast<FromAttr>(attrs[0])->bb;
    auto bb2 = cast<FromAttr>(attrs[1])->bb;
    if (!(bb1 == latch && bb2 == preheader
       || bb2 == latch && bb1 == preheader))
      return;
  }

  // If the loop has fixed lower and upper bound,
  // and it isn't too large (<= 32),
  // then fully unroll it.
  auto lower = loop->getStart();
  auto upper = loop->getStop();
  if (!upper)
    return;

  if (isa<IntOp>(lower) && isa<IntOp>(upper)) {
    int low = V(lower);
    int high = V(upper);
    if (high - low > 32)
      return;
    // Replicate every block.
    auto bb = loop->getPreheader();
    auto region = bb->getParent();

    for (auto block : loop->getBlocks()) {
      bb = region->insertAfter(bb);

    }
  }
}

void LoopUnroll::run() {
  LoopAnalysis analysis(module);
  analysis.run();
  const auto &forests = analysis.getResult();

  auto funcs = collectFuncs();
  for (auto func : funcs) {
    for (auto loop : forests.at(func).getLoops())
      runImpl(loop);
  }
}
