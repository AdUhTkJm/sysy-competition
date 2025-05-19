#include "Passes.h"
#include "../codegen/CodeGen.h"
#include "../codegen/Attrs.h"
#include <iterator>

using namespace sys;

std::map<std::string, int> Mem2Reg::stats() {
  return {
    { "lowered-alloca", count },
    { "missed-alloca", missed },
  };
}

// See explanation at https://longfangsong.github.io/en/mem2reg-made-simple/
void Mem2Reg::runImpl(FuncOp *func) {
  converted.clear();
  visited.clear();
  phiFrom.clear();
  domtree.clear();

  auto region = func->getRegion();
  region->updateDomFront();
  domtree = getDomTree(region);

  Builder builder;

  // We need to put PhiOp at places where a StoreOp doesn't dominate,
  // because it means at least 2 possible values.
  auto allocas = func->findAll<AllocaOp>();
  for (auto alloca : allocas) {
    bool good = true;

    // If the alloca is used for, as an example, AddOp, then
    // it's an array and can't be promoted to registers.
    for (auto use : alloca->getUses()) {
      if (!isa<LoadOp>(use) && !isa<StoreOp>(use)) {
        good = false;
        break;
      }
      // If the alloca is used as a value in a StoreOp, then it has to be an array.
      if (isa<StoreOp>(use) && use->DEF(0) == alloca) {
        good = false;
        break;
      }
    }

    if (!good) {
      missed++;
      continue;
    }
    count++;
    converted.insert(alloca);

    // Now find all blocks where stores reside in. Use set to de-duplicate.
    std::set<BasicBlock*> bbs;
    for (auto use : alloca->getUses()) {
      if (isa<StoreOp>(use))
        bbs.insert(use->getParent());
    }

    std::vector<BasicBlock*> worklist;
    std::copy(bbs.begin(), bbs.end(), std::back_inserter(worklist));

    std::set<BasicBlock*> visited;

    while (!worklist.empty()) {
      auto bb = worklist.back();
      worklist.pop_back();

      for (auto dom : bb->getDominanceFrontier()) {
        if (visited.count(dom))
          continue;
        visited.insert(dom);

        // Insert a PhiOp at the dominance frontier of each StoreOp, as described above.
        // The PhiOp is broken; we only record which AllocaOp it's from.
        // We'll fill it in later.
        builder.setToBlockStart(dom);
        auto phi = builder.create<PhiOp>();
        phiFrom[phi] = alloca;
        worklist.push_back(dom);
      }
    }
  }

  fillPhi(func->getRegion()->getFirstBlock(), {});

  for (auto alloca : converted)
    alloca->erase();
}

void Mem2Reg::fillPhi(BasicBlock *bb, SymbolTable symbols) {
  if (visited.count(bb))
    return;
  visited.insert(bb);

  Builder builder;

  std::vector<Op*> removed;
  for (auto op : bb->getOps()) {
    // Loads are now ordinary reads.
    if (auto load = dyn_cast<LoadOp>(op)) {
      auto alloca = load->getOperand().defining;
      if (!converted.count(alloca))
        continue;
  
      if (!symbols.count(alloca)) {
        builder.setBeforeOp(load);
        bool fp = alloca->has<FPAttr>();
        symbols[alloca] = fp
          ? (Op*) builder.create<FloatOp>({ new FloatAttr(0) })
          : (Op*) builder.create<IntOp>({ new IntAttr(0) });
      }
      
      load->replaceAllUsesWith(symbols[alloca].defining);
      removed.push_back(load);
    }
    
    // Stores are now mutating symbol table.
    if (auto store = dyn_cast<StoreOp>(op)) {
      auto value = store->getOperand(0);
      auto alloca = store->getOperand(1).defining;
      if (!converted.count(alloca))
        continue;
      symbols[alloca] = value;

      removed.push_back(store);
    }

    if (auto phi = dyn_cast<PhiOp>(op)) {
      if (!phiFrom.count(phi))
        continue;
      auto alloca = phiFrom[phi];
      symbols[alloca] = phi;
    }
  }

  for (auto succ : bb->getSuccs()) {
    auto phis = succ->getPhis();
    for (auto op : phis) {
      auto alloca = phiFrom[cast<PhiOp>(op)];

      // We meet a PhiOp. This means the promoted register might hold value `symbols[alloca]` when it reaches here.
      // So this PhiOp should have that value as operand as well.
      Value value;
      
      // It doesn't have an initial value from this path.
      // It's acceptable (for example a variable defined only in a loop)
      // Treat it as zero from this branch.
      if (!symbols.count(alloca)) {
        // Create a zero at the back of the incoming edge.
        auto term = bb->getLastOp();
        builder.setBeforeOp(term);
        value = builder.create<IntOp>({ new IntAttr(0) });
      } else
        value = symbols[alloca];

      op->pushOperand(value);
      op->add<FromAttr>(bb);
    }
  }

  for (auto x : removed)
    x->erase();
  
  for (auto child : domtree[bb])
    fillPhi(child, symbols);
}

void Mem2Reg::run() {
  auto funcs = collectFuncs();
  for (auto func : funcs)
    runImpl(func);
}
