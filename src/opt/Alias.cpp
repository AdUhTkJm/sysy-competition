#include "Passes.h"

using namespace sys;

using DomTree = std::unordered_map<BasicBlock*, std::vector<BasicBlock*>>;

void postorder(BasicBlock *current, DomTree &tree, std::vector<BasicBlock*> &order) {
  for (auto bb : tree[current])
    postorder(bb, tree, order);
  order.push_back(current);
}

void Alias::runImpl(Region *region) {
  // Run local analysis over RPO of the dominator tree.

  // First calculate RPO.
  DomTree tree;
  for (auto bb : region->getBlocks()) {
    if (auto idom = bb->getIdom())
      tree[idom].push_back(bb);
  }

  BasicBlock *entry = region->getFirstBlock();
  std::vector<BasicBlock*> rpo;
  postorder(entry, tree, rpo);
  std::reverse(rpo.begin(), rpo.end());

  // Then traverse the CFG in that order.
  // This should guarantee definition comes before all uses.
  for (auto bb : rpo) {
    for (auto op : bb->getOps()) {
      if (isa<AllocaOp>(op) && !op->has<AliasAttr>()) {
        op->add<AliasAttr>(op, 0);
        continue;
      }

      if (isa<GetGlobalOp>(op) && !op->has<AliasAttr>()) {
        op->add<AliasAttr>(gMap[NAME(op)], 0);
        continue;
      }
      
      if (isa<AddLOp>(op)) {
        op->remove<AliasAttr>();
        auto x = op->getOperand(0).defining;
        auto y = op->getOperand(1).defining;
        if (!(x->has<AliasAttr>() || y->has<AliasAttr>())) {
          op->add<AliasAttr>(/*unknown*/);
          continue;
        }

        if (!x->has<AliasAttr>())
          std::swap(x, y);

        // Now `x` is the address and `y` is the offset. 
        // Note this swap won't affect the original op.
        auto alias = x->get<AliasAttr>()->clone();
        if (isa<IntOp>(y)) {
          auto delta = V(y);
          for (size_t i = 0; i < alias->location.size(); i++)
            // Meaning: offset += delta;
            alias->location[i].second += delta;
        } else {
          // Unknown offset. Set all offsets to -1.
          for (size_t i = 0; i < alias->location.size(); i++)
            alias->location[i].second = -1;
        }

        op->add<AliasAttr>(alias->location);
        delete alias;
        continue;
      }
    }
  }
}

// This only works after Mem2Reg, because it needs the guarantee that no `int**` is possible.
// Before Mem2Reg, we can store the address of an array in an alloca.
// Moreover, it's only useful when all unnecessary alloca's have been removed.
void Alias::run() {
  auto funcs = collectFuncs();
  gMap = getGlobalMap();

  for (auto func : funcs)
    runImpl(func->getRegion());
}
