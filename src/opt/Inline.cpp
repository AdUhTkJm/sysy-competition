#include "Passes.h"
#include "Analysis.h"

using namespace sys;

std::map<std::string, int> Inline::stats() {
  return {
    { "inlined-functions", inlined }
  };
}

bool isRecursive(Op *op) {
  const auto &callers = CALLER(op);
  const auto &name = NAME(op);
  return std::find(callers.begin(), callers.end(), name) != callers.end();
}

// This pass must run before Mem2Reg but after FlattenCFG.
void Inline::run() {
  CallGraph(module).run();
  
  Builder builder;

  fnMap = getFunctionMap();

  runRewriter([&](CallOp *call) {
    const auto &fname = NAME(call);
    if (isExtern(fname))
      return false;

    if (!fnMap.count(fname)) {
      std::cerr << "unknown function: " << fname << "\n";
      assert(false);
    }

    FuncOp *func = fnMap[fname];

    // Don't inline overly large functions.
    auto fnRegion = func->getRegion();
    int opcount = 0;
    for (auto bb : fnRegion->getBlocks())
      opcount += bb->getOps().size();
    if (opcount >= threshold)
      return false;

    // Don't inline recursive functions here, otherwise this rewriter will loop forever.
    // Deal with them later.
    if (isRecursive(func))
      return false;

    // Maps old Op to new Op.
    std::map<Op*, Op*> cloneMap;
    std::map<BasicBlock*, BasicBlock*> retargetMap;
    auto bb = call->getParent();
    auto callRegion = bb->getParent();
    auto end = callRegion->insertAfter(bb);
    bb->splitOpsAfter(end, call);

    // Current situation:
    // bb0:
    //    ....
    // bb1:
    //    call
    //    ....
    // Insert a proper amount of blocks between `bb` and `end`.
    std::vector<BasicBlock*> body;
    for (auto bb : fnRegion->getBlocks())
      body.push_back(callRegion->insert(end));

    builder.setToBlockEnd(bb);
    // The address of return value.
    // It can only be 4 bytes because no pointers can be returned.
    auto addr = builder.create<AllocaOp>({ new SizeAttr(4) });

    // Copy the operations block by block.
    int i = 0;
    for (auto bb : fnRegion->getBlocks()) {
      builder.setToBlockStart(body[i]);
      retargetMap[bb] = body[i];
      i++;

      for (auto op : bb->getOps()) {
        auto shallow = builder.copy(op);
        cloneMap[op] = shallow;
        
        // We can safely remap arguments here, as there's no phi before Mem2Reg.
        auto operands = shallow->getOperands();
        shallow->removeAllOperands();
        for (auto operand : operands) {
          auto def = operand.defining;
          if (!cloneMap.count(def)) op->dump(std::cerr), def->dump(std::cerr);
          assert(cloneMap.count(def));
          shallow->pushOperand(cloneMap[def]);
        }
      }
    }

    // Connect the blocks together.
    assert(body.size());
    builder.setToBlockEnd(bb);
    builder.create<GotoOp>({ new TargetAttr(body[0]) });

    // Rewrite operations.
    for (auto [_, v] : cloneMap) {
      // Rewire jump targets.
      // As it's a shallow copy, we need to create a new one to avoid affecting the original version.
      if (auto attr = v->find<TargetAttr>()) {
        assert(retargetMap.count(attr->bb));
        attr->bb = retargetMap[attr->bb];
      }
      if (auto attr = v->find<ElseAttr>()) {
        assert(retargetMap.count(attr->bb));
        attr->bb = retargetMap[attr->bb];
      }

      if (isa<GetArgOp>(v)) {
        auto i = V(v);
        auto def = call->getOperand(i).defining;
        v->replaceAllUsesWith(def);
        v->erase();
        continue;
      }

      if (isa<ReturnOp>(v)) {
        if (v->getOperands().size() == 0) {
          builder.replace<GotoOp>(v, { new TargetAttr(end) });
          continue;
        }

        auto ret = v->getOperand().defining;
        builder.setBeforeOp(v);
        builder.create<StoreOp>({ ret, addr }, { new SizeAttr(4) });
        builder.replace<GotoOp>(v, { new TargetAttr(end) });
        continue;
      }
    }

    builder.setBeforeOp(call);
    auto load = builder.create<LoadOp>(call->getResultType(), { addr });
    call->replaceAllUsesWith(load);
    call->erase();
    return true;
  });

  // New alloca's have been introduced. Move them to the top.
  auto funcs = collectFuncs();
  
  for (auto func : funcs) {
    auto allocas = func->findAll<AllocaOp>();
    auto region = func->getRegion();
    auto begin = region->getFirstBlock();
    
    // It's possible we've inlined a function into another one without alloca's.
    // In that case we must create a new block for it.
    if (!isa<AllocaOp>(begin->getFirstOp())) {
      auto last = begin;
      begin = region->insert(begin);
      builder.setToBlockEnd(begin);
      builder.create<GotoOp>({ new TargetAttr(last) });
    }

    for (auto alloca : allocas)
      alloca->moveBefore(begin->getLastOp());
  }
}
