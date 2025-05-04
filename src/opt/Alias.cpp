#include "Passes.h"

using namespace sys;

void postorder(BasicBlock *current, DomTree &tree, std::vector<BasicBlock*> &order) {
  for (auto bb : tree[current])
    postorder(bb, tree, order);
  order.push_back(current);
}

void Alias::runImpl(Region *region) {
  // Run local analysis over RPO of the dominator tree.

  // First calculate RPO.
  DomTree tree = getDomTree(region);

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
        if (!x->has<AliasAttr>() && !y->has<AliasAttr>()) {
          op->add<AliasAttr>(/*unknown*/);
          continue;
        }

        if (!x->has<AliasAttr>())
          std::swap(x, y);

        // Now `x` is the address and `y` is the offset. 
        // Note this swap won't affect the original op.
        auto alias = ALIAS(x)->clone();
        if (isa<IntOp>(y)) {
          auto delta = V(y);
          for (auto &[_, offset] : alias->location) {
            for (auto &value : offset)
              value += delta;
          }
        } else {
          // Unknown offset. Set all offsets to -1.
          for (auto &[_, offset] : alias->location)
            offset = { -1 };
        }

        op->add<AliasAttr>(alias->location);
        delete alias;
        continue;
      }
    }
  }
}

// This has better precision after Mem2Reg, because less `int**` is possible.
// Before Mem2Reg, we can store the address of an array in an alloca. 
// (Though it won't be fully eliminated after the pass; see 66_exgcd.sy)
// Moreover, it's more useful when all unnecessary alloca's have been removed.
//
// In addition, remember to update the information after Globalize and Localize.
void Alias::run() {
  auto funcs = collectFuncs();
  gMap = getGlobalMap();

  for (auto func : funcs)
    runImpl(func->getRegion());

  // Now do a dataflow analysis on call graph.
  auto fnMap = getFunctionMap();
  std::vector<FuncOp*> worklist { fnMap["main"] };
  while (!worklist.empty()) {
    auto func = worklist.back();
    worklist.pop_back();

    // Update local alias info.
    runImpl(func->getRegion());

    // Find all CallOps from the function.
    auto calls = func->findAll<CallOp>();

    for (auto call : calls) {
      const auto &name = NAME(call);
      bool changed = false;

      // Propagate alias info for each argument.
      if (isExtern(name))
        continue;

      runRewriter(fnMap[name], [&](GetArgOp *op) {
        int index = V(op);
        auto def = call->getOperand(index).defining;
        if (!def->has<AliasAttr>())
          return false;

        auto defLoc = ALIAS(def);
        if (op->has<AliasAttr>())
          changed |= ALIAS(op)->addAll(defLoc);
        else {
          op->add<AliasAttr>(*defLoc);
          changed = true;
        }
        
        // Do it only once.
        return false;
      });

      if (changed)
        worklist.push_back(fnMap[name]);
    }
  }
}
