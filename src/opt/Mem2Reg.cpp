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
  func->getRegion()->updateDoms();
  converted.clear();
  visited.clear();
  symbols.clear();
  phiFrom.clear();

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

  fillPhi(func->getRegion()->getFirstBlock(), nullptr);
}

void Mem2Reg::fillPhi(BasicBlock *bb, BasicBlock *last) {
  Builder builder;

  for (auto op : bb->getOps()) {
    if (!isa<PhiOp>(op))
      // phi's are always at the front.
      break; 

    auto alloca = phiFrom[cast<PhiOp>(op)];
    
    // It doesn't have an initial value from this path.
    // It's acceptable (programs without UB can also do this, see official/25.sy)
    // Simply ignore phi from this branch.
    if (!symbols.count(alloca))
      continue;

    // We meet a PhiOp. This means the promoted register might hold value `symbols[alloca]` when it reaches here.
    // So this PhiOp should have that value as operand as well.
    auto value = symbols[alloca];
    // Found the same op. Alright to skip it.
    if (value.defining == op) {
      continue;
    }

    op->pushOperand(value);
    assert(last);
    op->addAttr<FromAttr>(last);
    symbols[alloca] = op;
  }

  if (visited.count(bb))
    return;
  visited.insert(bb);

  std::vector<std::pair<LoadOp*, Value>> loads;
  std::vector<StoreOp*> stores;
  for (auto op : bb->getOps()) {
    // Loads are now ordinary reads.
    if (auto load = dyn_cast<LoadOp>(op)) {
      auto alloca = load->getOperand().defining;
      // This is a global variable.
      if (!converted.count(alloca))
        continue;
  
      // It's possible for arrays - in which case it isn't undefined behaviour.
      if (!symbols.count(alloca)) {
        builder.setBeforeOp(alloca);
        symbols[alloca] = builder.create<IntOp>({ new IntAttr(0) });
      }
      
      auto value = symbols[alloca];
      loads.push_back(std::make_pair(load, value));
    }
    
    // Stores are now mutating symbol table.
    if (auto store = dyn_cast<StoreOp>(op)) {
      auto value = store->getOperand(0);
      auto alloca = store->getOperand(1).defining;
      if (!converted.count(alloca))
        continue;
      symbols[alloca] = value;

      stores.push_back(store);
    }

    if (auto phi = dyn_cast<PhiOp>(op)) {
      if (!phiFrom.count(phi))
        continue;
      auto alloca = phiFrom[phi];
      symbols[alloca] = phi;
    }

    if (auto branch = dyn_cast<BranchOp>(op)) {
      {
        SemanticScope scope(*this);
        fillPhi(branch->getAttr<TargetAttr>()->bb, bb);
      }
      SemanticScope scope(*this);
      fillPhi(branch->getAttr<ElseAttr>()->bb, bb);
    }

    if (auto jmp = dyn_cast<GotoOp>(op))
      fillPhi(jmp->getAttr<TargetAttr>()->bb, bb);
  }

  for (auto [load, value] : loads) {
    load->replaceAllUsesWith(value.defining);
    load->erase();
  }

  for (auto store : stores)
    store->erase();
}

void Mem2Reg::run() {
  auto funcs = module->findAll<FuncOp>();
  for (auto func : funcs)
    runImpl(func);
}
