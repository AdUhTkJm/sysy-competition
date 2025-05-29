#include "Passes.h"
#include "Analysis.h"

using namespace sys;

std::map<std::string, int> LateInline::stats() {
  return {
    { "inlined-functions", inlined }
  };
}

// Defined in Inline.cpp.
bool isRecursive(Op *op);

// This pass runs after Mem2Reg.
void LateInline::run() {
  CallGraph(module).run();
  
  Builder builder;

  auto fnMap = getFunctionMap();

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
      opcount += bb->getOpCount();
    if (opcount >= threshold)
      return false;

    // Don't inline recursive functions here.
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
    // All return values.
    std::vector<std::pair<Op*, BasicBlock*>> returns;

    // Copy the operations block by block.
    int i = 0;
    for (auto bb : fnRegion->getBlocks()) {
      builder.setToBlockStart(body[i]);
      retargetMap[bb] = body[i];
      i++;

      for (auto op : bb->getOps()) {
        auto shallow = builder.copy(op);
        cloneMap[op] = shallow;
      }
    }

    // Rewire operations.
    for (auto bb : body) {
      for (auto op : bb->getOps()) {
        auto operands = op->getOperands();
        op->removeAllOperands();
        for (auto operand : operands) {
          auto def = operand.defining;
          if (!cloneMap.count(def)) {
            std::cerr << module << op << def;
            assert(false);
          }
          op->pushOperand(cloneMap[def]);
        }
      }
    }

    // Get phis right.
    for (auto bb : body) {
      auto phis = bb->getPhis();
      for (auto phi : phis) {
        for (auto attr : phi->getAttrs())
          FROM(attr) = retargetMap[FROM(attr)];
      }
    }

    // Connect the blocks together.
    assert(body.size());
    builder.setToBlockEnd(bb);
    builder.create<GotoOp>({ new TargetAttr(body[0]) });

    // Rewrite operations.
    for (auto [_, v] : cloneMap) {
      // Rewire jump targets.
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
        auto def = call->DEF(i);
        v->replaceAllUsesWith(def);
        v->erase();
        continue;
      }
    }

    for (auto [_, v] : cloneMap) {
      if (isa<ReturnOp>(v)) {
        if (v->getOperands().size() == 0) {
          builder.replace<GotoOp>(v, { new TargetAttr(end) });
          continue;
        }

        auto retval = v->DEF();
        builder.setBeforeOp(v);
        returns.push_back({ retval, v->getParent() });
        builder.replace<GotoOp>(v, { new TargetAttr(end) });
        continue;
      }
    }

    // We've split everything after call (inclusive) to a new block, 
    // so call is now at the top of the block.
    // Create a phi before it.
    builder.setBeforeOp(call);
    if (returns.size() > 0) {
      if (returns.size() == 1) {
        call->replaceAllUsesWith(returns[0].first);
      } else {
        auto phi = builder.create<PhiOp>();
        for (auto [op, bb] : returns) {
          phi->pushOperand(op);
          phi->add<FromAttr>(bb);
        }
        call->replaceAllUsesWith(phi);
      }
    }
    call->erase();

    // Phi's that point to the block before the call should go from `end` instead.
    // As preds haven't been updated it's alright.
    for (auto succ : bb->succs) {
      auto phis = succ->getPhis();
      for (auto phi : phis) {
        for (auto attr : phi->getAttrs()) {
          auto &from = FROM(attr);
          if (from == bb)
            from = end;
        }
      }
    }

    // This is necessary, because we might inline two calls in the same block.
    // We need to update preds so that the second call sees correct CFG.
    bb->getParent()->updatePreds();
    return true;
  });

  // Move allocas to the front. We might've inlined some allocas to the middle of the function.
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
      alloca->moveBefore(begin->getFirstOp());
  }
}
