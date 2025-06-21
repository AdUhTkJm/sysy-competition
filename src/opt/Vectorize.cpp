#include "LoopPasses.h"

using namespace sys;

// We can't just use the `Base` pass,
// as it doesn't handle phis at all.
Op *Vectorize::findBase(Op *op) {
  if (base.count(op))
    return base[op];

  if (isa<AddLOp>(op)) {
    // It's possible for the address to be at 2nd position.
    // but it should have been normalized.
    return base[op] = findBase(op->DEF(0));
  }

  if (isa<AllocaOp>(op) || isa<GetGlobalOp>(op))
    return base[op] = op;

  if (isa<PhiOp>(op)) {
    const auto &operands = op->getOperands();
    base[op] = op;
    auto b0 = findBase(operands[0].defining);
    base[op] = b0;
    for (int i = 1; i < operands.size(); i++) {
      auto b = findBase(operands[i].defining);
      if (base[op] == op && b0 != op)
        base[op] = b0;
      
      if (b != base[op])
        return base[op] = nullptr;
    }
    return base[op] = b0;
  }

  // It doesn't have a base. (It might not even be a pointer.)
  return base[op] = nullptr;
}

void Vectorize::runImpl(LoopInfo *info) {
  if (info->latches.size() > 1)
    return;

  auto header = info->header;
  auto latch = info->getLatch();

  // Ensure no branching except for the latch.
  for (auto bb : info->getBlocks()) {
    auto term = bb->getLastOp();
    if (isa<BranchOp>(term) && bb != latch) {
      return;
    }
  }

  // Ensure no calls anywhere.
  for (auto bb : info->getBlocks()) {
    for (auto op : bb->getOps()) {
      if (isa<CallOp>(op))
        return;
    }
  }

  auto phis = header->getPhis();
  std::unordered_set<Op*> bases;
  
  // Ensure all phis have stride 4 and different bases.
  // (Stride 4 is to ensure loop step is 1; larger steps will hit memory wall)
  for (auto phi : phis) {
    auto latchval = Op::getPhiFrom(phi, latch);
    if (!isa<AddLOp>(latchval)) {
      if (isa<AddIOp>(latchval) && isa<IntOp>(latchval->DEF(1)))
        continue;

      // Other phi nodes cannot easily get mutated.
      return;
    }

    auto base = findBase(latchval);
    if (!base || !isa<IntOp>(latchval->DEF(1)) || V(latchval->DEF(1)) != 4)
      return;

    if (bases.count(base))
      return;
    bases.insert(base);
  }

  // Ensure the same base isn't both read and written in the same iteration.
  std::unordered_set<Op*> stored, loaded;
  std::vector<Op*> loads, stores, addrs;
  for (auto bb : info->getBlocks()) {
    for (auto op : bb->getOps()) {
      if (isa<StoreOp>(op))
        stored.insert(findBase(op->DEF(1))),
        addrs.push_back(op->DEF(1)),
        stores.push_back(op);

      if (isa<LoadOp>(op))
        loaded.insert(findBase(op->DEF(0))),
        addrs.push_back(op->DEF(0)),
        loads.push_back(op);
    }
  }

  // Accessed unknown places.
  if (stored.count(nullptr) || loaded.count(nullptr))
    return;

  // Ensure we only read/store to those phis.
  std::unordered_set<Op*> phiset(phis.begin(), phis.end());
  for (auto x : addrs) {
    if (!phiset.count(x))
      return;
  }

  // Start rewriting.
  std::vector<Op*> erased, created;
  bool success = true;

  std::vector<Op*> queue(stores.begin(), stores.end());
  
#define BAD(x) if (x) { std::cerr << "bad on " #x "\n"; success = false; break; }

  Builder builder;
  std::unordered_map<Op*, Op*> opmap;
  std::unordered_set<Op*> visited;

  const auto &replace = [&](Op *old, Op *op) {
    opmap[old] = op;
    created.push_back(op);
    erased.push_back(old);
  };

  // First deal with all loads.
  for (auto load : loads) {
    if (load->getResultType() != Value::i32) {
      success = false;
      break;
    }
    
    visited.insert(load);
    builder.setBeforeOp(load);

    auto ld = builder.create<LoadOp>(Value::i128, load->getOperands(), { new SizeAttr(16) });
    replace(load, ld);
    
    for (auto x : load->getUses())
      queue.push_back(x);
  }

  while (success && !queue.empty()) {
    // Pop from one end, and make sure the initial stores lie on the other end.
    // This will hopefully delay the stores after all their operands are prepared.
    auto x = queue.back();
    queue.pop_back();

    if (visited.count(x))
      continue;
    visited.insert(x);

    builder.setBeforeOp(x);
    switch (x->opid) {
    case LoadOp::id: {
      // These loads are definitely outside the loop.
      // All loads inside this loop have been processed above.
      // Moreover, as all stores in this loop only stores to some of the phis,
      // the load must be loop-invariant.
      // Therefore just record it as it is.
      assert(!info->contains(x->getParent()));
      opmap[x] = x;
      break;
    }
    case StoreOp::id: {
      auto value = x->DEF(0);
      // Optimize memset-like operations.
      if (isa<IntOp>(value) || !info->contains(value->getParent())) {
        auto b = builder.create<BroadcastOp>({ value });
        created.push_back(b);
        auto st = builder.create<StoreOp>({ b, x->DEF(1) }, { new SizeAttr(16) });
        replace(x, st);
        break;
      }

      BAD(!opmap.count(value) || opmap[value]->getResultType() != Value::i128);
      auto st = builder.create<StoreOp>({ opmap[value], x->DEF(1) }, { new SizeAttr(16) });
      replace(x, st);
      break;
    }
    case AddIOp::id: {
      auto a = x->DEF(0), b = x->DEF(1);
      if (opmap.count(a) && opmap.count(b)) {
        auto aty = a->getResultType();
        auto bty = b->getResultType();
        if (aty == bty && aty == Value::i128) {
          auto vop = builder.create<AddVOp>({ opmap[a]->getResult(), opmap[b] });
          replace(x, vop);
          break;
        }
      }
      if (opmap.count(b) && !opmap.count(a))
        std::swap(a, b);
      if (opmap.count(a) && !opmap.count(b)) {
        auto aty = a->getResultType();
        auto bty = b->getResultType();
        
        // `b` has to be loop invariant.
        // TODO: when `b` itself is loop counter, perhaps optimizable?
        BAD(info->contains(b->getParent()));

        if (aty == Value::i128 && bty == Value::i32) {
          auto broadcast = builder.create<BroadcastOp>({ b });
          created.push_back(broadcast);
          auto viop = builder.create<AddVOp>({ opmap[a]->getResult(), broadcast });
          replace(x, viop);
          break;
        }
        if (aty == Value::i32 && bty == Value::i32) {
          auto add = builder.create<AddIOp>({ opmap[a]->getResult(), b });
          replace(x, add);
          break;
        }
      }
      success = false;
      break;
    }
    // TODO: check PhiOp and deal with accumulator?
    default:
      std::cerr << "met unhandlable " << x;
      success = false;
      break;
    }
    for (auto x : x->getUses())
      queue.push_back(x);
  }

  if (!success) {
    // Undo operations.
    std::cerr << "undo for loop " << bbmap[info->header] << "\n";
    for (auto op : created)
      op->removeAllOperands();
    for (auto op : created)
      op->erase();
    return;
  }

  // Success; commit operations and erase the original ones.
  std::cerr << "success, vectorized loop " << bbmap[info->header] << "\n";
  for (auto op : erased) {
    op->replaceAllUsesWith(opmap[op]);
    op->erase();
  }

  // The stride is quadrapled.
  for (auto phi : phis) {
    auto latchval = Op::getPhiFrom(phi, latch);

    if (isa<AddIOp>(latchval) && isa<IntOp>(latchval->DEF(1))) {
      builder.setBeforeOp(latchval);
      auto more = builder.create<IntOp>({ new IntAttr(V(latchval->DEF(1)) * 4) });
      latchval->setOperand(1, more);
    }

    // We've ensured it's always 4 for every addl in question.
    if (isa<AddLOp>(latchval)) {
      builder.setBeforeOp(latchval);
      auto more = builder.create<IntOp>({ new IntAttr(16) });
      latchval->setOperand(1, more);
    }
  }
}

void Vectorize::run() {
  LoopAnalysis analysis(module);
  analysis.run();
  auto forests = analysis.getResult();

  auto funcs = collectFuncs();
  
  for (auto func : funcs) {
    const auto &forest = forests[func];
    auto region = func->getRegion();

    // Only vectorize innermost loops.
    for (auto loop : forest.getLoops()) {
      if (!loop->subloops.size())
        runImpl(loop);
    }
  }
}
