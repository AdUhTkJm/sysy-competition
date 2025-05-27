#include "CleanupPasses.h"
#include "Analysis.h"
#include "../codegen/Attrs.h"

using namespace sys;

std::map<std::string, int> DCE::stats() {
  return {
    { "removed-ops", elimOp },
    { "removed-basic-blocks", elimBB },
    { "removed-funcs", elimFn },
  };
}

bool DCE::isImpure(Op *op) {
  if (isa<StoreOp>(op) || isa<ReturnOp>(op) ||
      isa<BranchOp>(op) || isa<GotoOp>(op) ||
      isa<ProceedOp>(op) || isa<BreakOp>(op) ||
      isa<ContinueOp>(op))
    return true;

  if (isa<CallOp>(op)) {
    auto name = NAME(op);
    if (isExtern(name))
      return true;
    return fnMap[name]->has<ImpureAttr>();
  }

  return false;
}

bool DCE::markImpure(Region *region) {
  bool impure = false;
  for (auto bb : region->getBlocks()) {
    for (auto op : bb->getOps()) {
      bool opImpure = false;
      if (isImpure(op))
        opImpure = true;
      for (auto r : op->getRegions())
        opImpure |= markImpure(r);

      if (opImpure && !op->has<ImpureAttr>()) {
        impure = true;
        op->add<ImpureAttr>();
      }
    }
  }
  return impure;
}

void DCE::runOnRegion(Region *region) {
  for (auto bb : region->getBlocks()) {
    for (auto op : bb->getOps()) {
      if (!op->has<ImpureAttr>() && op->getUses().size() == 0)
        removeable.push_back(op);
      else for (auto r : op->getRegions())
        runOnRegion(r);
    }
  }
}

void DCE::run() {
  auto funcs = collectFuncs();
  fnMap = getFunctionMap();
  
  for (auto func : funcs)
    markImpure(func->getRegion());
  
  // Remove unused variables.
  do {
    removeable.clear();
    for (auto func : funcs)
      runOnRegion(func->getRegion());

    elimOp += removeable.size();
    for (auto op : removeable)
      op->erase();
  } while (removeable.size());

  // Remove unused phi's. 
  // They might be cyclically referencing each other, but not used elsewhere.
  std::vector<Op*> unused;
  runRewriter([&](PhiOp *op) {
    if (!op->getOperandCount())
      return false;
    
    std::vector<Op*> worklist { op };
    std::set<Op*> visited;

    while (!worklist.empty()) {
      auto *op = worklist.back();
      worklist.pop_back();
      visited.insert(op);

      for (auto *use : op->getUses()) {
        if (visited.count(use))
          continue;
        
        if (isa<PhiOp>(use))
          worklist.push_back(use);
        else
          return false; 
      }
    }

    // Here all phi's in worklist are dead.
    for (auto phi : visited) {
      phi->removeAllOperands();
      unused.push_back(phi);
    }
    return true;
  });

  for (auto dead : unused)
    dead->erase();

  // Remove unused functions.
  bool changed;
  do {
    CallGraph(module).run();

    changed = false;
    for (auto func : funcs) {
      const auto &name = NAME(func);
      // main() might not be used, but it must be preserved.
      if (name == "main")
        continue;

      // If a function is only used by itself, then it's also unused.
      const auto &callers = CALLER(func);
      if (!callers.size() || (callers.size() == 1 && callers[0] == name)) {
        func->erase();
        changed = true;
        elimFn++;
      }
    }

    if (changed)
      funcs = collectFuncs();
  } while (changed);

  if (!elimBlocks)
    return;
  
  do {
    changed = false;
    
    for (auto func : funcs) {
      auto region = func->getRegion();
      region->updatePreds();
      std::set<BasicBlock*> toRemove;
      auto entry = region->getFirstBlock();

      std::set<BasicBlock*> reachable;
      std::vector<BasicBlock*> queue { entry };
      while (!queue.empty()) {
        auto bb = queue.back();
        queue.pop_back();
        
        if (reachable.count(bb))
          continue;
        reachable.insert(bb);
        for (auto succ : bb->succs)
          queue.push_back(succ);
      }

      for (auto bb : region->getBlocks()) {
        if (!reachable.count(bb))
          toRemove.insert(bb);
      }

      elimBB += toRemove.size();
      if (toRemove.size())
        changed = true;

      // Remove all operands first, to avoid inter-dependency between blocks.
      for (auto bb : toRemove) {
        for (auto op : bb->getOps())
          op->removeAllOperands();
      }

      // Remove phi nodes that take these dead blocks as input.
      for (auto bb : region->getBlocks()) {
        auto phis = bb->getPhis();
        for (auto phi : phis) {
          auto ops = phi->getOperands();
          std::vector<Attr*> attrs;
          for (auto attr : phi->getAttrs())
            attrs.push_back(attr->clone());

          phi->removeAllOperands();
          // This deletes attributes if their refcnt goes to zero.
          // That's why we cloned above.
          phi->removeAllAttributes();

          for (size_t i = 0; i < ops.size(); i++) {
            auto from = FROM(attrs[i]);
            if (toRemove.count(ops[i].defining->getParent()) || toRemove.count(from))
              continue;

            // Only preserve the operands that aren't from dead blocks.
            phi->pushOperand(ops[i]);
            phi->add<FromAttr>(from);
          }

          // The added attributes are also copies.
          // No need to preserve the ones in this vector.
          for (auto attr : attrs)
            delete attr;
        }
      }

      // Do the real removal.
      for (auto bb : toRemove)
        bb->forceErase();
    }
  } while (changed);
}
