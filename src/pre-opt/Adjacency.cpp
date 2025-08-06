#include "PreLoopPasses.h"
#include "PreAnalysis.h"

using namespace sys;

void Adjacency::runImpl(Op *loop) {
  auto loads = loop->findAll<LoadOp>();
  std::vector<Op*> addrs;
  addrs.reserve(loads.size());
  for (auto load : loads) {
    auto addr = load->DEF();
    if (!addr->has<BaseAttr>() || !addr->has<SubscriptAttr>())
      return;
    addrs.push_back(addr);
  }

  for (auto addr : addrs) {
    auto subscript = SUBSCRIPT(addr);
    if (subscript.back() != 0)
      continue;

    // This is `x[i]`. Try to find `x[i + 1]` with the same `x`.
    auto desired = subscript;
    desired.back() = 4;
    Op *adj = nullptr;
    for (auto x : addrs) {
      if (SUBSCRIPT(x) == desired) {
        adj = x;
        break;
      }
    }
    if (!adj)
      continue;
    
  }
}

void Adjacency::run() {
  Base(module).run();
  ArrayAccess(module).run();

  auto loops = module->findAll<ForOp>();
  for (auto loop : loops)
    runImpl(loop);
}
