#include "Passes.h"
#include "../codegen/CodeGen.h"
#include "../codegen/Attrs.h"

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
  std::copy(allocas.begin(), allocas.end(), std::inserter(converted, converted.end()));
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

    // Now find all blocks where stores reside in. Use set to de-duplicate.
    std::set<BasicBlock*> bbs;
    for (auto use : alloca->getUses()) {
      if (isa<StoreOp>(use))
        bbs.insert(use->getParent());
    }

    std::set<BasicBlock*> needed;
    for (auto bb : bbs) {
      for (auto one : bb->getDominanceFrontier())
        needed.insert(one);
    }

    for (auto bb : needed) {
      // Insert a PhiOp at the dominance frontier of each StoreOp, as described above.
      // The PhiOp is broken; we only record which AllocaOp it's from.
      // We'll fill it in later.
      builder.setToBlockStart(bb);
      auto phi = builder.create<PhiOp>();
      phiFrom[phi] = alloca;
    }
  }

  fillPhi(func->getRegion()->getFirstBlock());
}

void Mem2Reg::fillPhi(BasicBlock *bb) {
  Builder builder;

  for (auto op : bb->getOps()) {
    if (!isa<PhiOp>(op))
      // phi's are always at the front.
      break; 

    auto alloca = phiFrom[cast<PhiOp>(op)];
    
    // Undefined behaviour in source program. Terminate.
    if (!symbols.count(alloca))
      assert(false);

    // We meet a PhiOp. This means the promoted register might hold value `symbols[alloca]` when it reaches here.
    // So this PhiOp should have that value as operand as well.
    auto value = symbols[alloca];
    op->pushOperand(value);
    symbols[alloca] = op;
  }

  if (visited.count(bb))
    return;
  visited.insert(bb);

  std::vector<LoadOp*> loads;
  std::vector<StoreOp*> stores;
  for (auto op : bb->getOps()) {
    // Loads are now ordinary reads.
    if (auto load = dyn_cast<LoadOp>(op))
      loads.push_back(load);
    
    // Stores are now mutating symbol table.
    if (auto store = dyn_cast<StoreOp>(op)) {
      auto value = store->getOperand(0);
      auto alloca = store->getOperand(1).defining;
      if (!converted.count(alloca))
        continue;
      symbols[alloca] = value;

      stores.push_back(store);
    }

    if (auto branch = dyn_cast<BranchOp>(op)) {
      {
        SemanticScope scope(*this);
        fillPhi(branch->getAttr<TargetAttr>()->bb);
      }
      SemanticScope scope(*this);
      fillPhi(branch->getAttr<ElseAttr>()->bb);
    }

    if (auto jmp = dyn_cast<GotoOp>(op))
      fillPhi(jmp->getAttr<TargetAttr>()->bb);
  }

  for (auto load : loads) {
    auto alloca = load->getOperand().defining;
    // This is a global variable.
    if (!converted.count(alloca))
      continue;

    // Undefined behaviour in source program. Terminate.
    if (!symbols.count(alloca)) {
      std::cerr << "cannot find value for this alloca:\n  ";
      alloca->dump(std::cerr);
      std::cerr << "its uses:\n";
      for (auto x : alloca->getUses()) {
        std::cerr << "  ";
        x->dump(std::cerr);
      }
      assert(false);
    }
    
    auto value = symbols[alloca];
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
