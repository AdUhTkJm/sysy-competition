#include "PreLoopPasses.h"
#include "../utils/Matcher.h"

using namespace sys;

std::map<std::string, int> Unswitch::stats() {
  return {
    { "unswitched-loops", unswitched },
  };
}

namespace {

Rule cmpmod("(eq (mod x 'a) 'b)");
Rule addmod("(mod (add x 'a) 'b)");

// It's guaranteed that the induction variable will be a multiple of `vi`
// at the beginning of the loop.
void tidymod(Op *loop, int vi) {
  int start = V(loop->DEF(0));
  auto region = loop->getRegion();
  auto entry = region->getFirstBlock();
  Builder builder;

  auto ops = entry->getOps();
  for (auto op : ops) {
    if (!isa<ModIOp>(op))
      continue;

    // This is a direct mod. See `start` to check.
    if (op->DEF(0) == loop && isa<IntOp>(op->DEF(1))) {
      auto v = V(op->DEF(1));
      if (vi % v)
        continue;
      builder.replace<IntOp>(op, { new IntAttr(start % v) });
    }

    if (!addmod.match(op, { { "x", loop } }))
      continue;

    auto incr = V(addmod.extract("'a"));
    auto mod = V(addmod.extract("'b"));

    if (vi % mod)
      continue;
    builder.replace<IntOp>(op, { new IntAttr((start + incr) % mod) });
  }
}

}

void unroll(Op *loop, int vi) {
  auto region = loop->getRegion();
  auto entry = region->getFirstBlock();

  Builder builder;
  std::list<Op*> body = entry->getOps();
  std::unordered_map<Op*, Op*> opmap;

  const std::function<void (Op*)> copy = [&](Op *x) {
    auto copied = builder.copy(x);
    opmap[x] = copied;

    for (auto r : x->getRegions()) {
      Builder::Guard guard(builder);
      
      auto cr = copied->appendRegion();

      auto entry = r->getFirstBlock();
      auto cEntry = cr->appendBlock();
      builder.setToBlockStart(cEntry);
      for (auto op : entry->getOps())
        copy(op);
    }
  };

  // Copy the loop body.
  for (int z = 1; z < vi; z++) {
    opmap.clear();
    builder.setToBlockEnd(entry);

    // Now the induction variable should be `i + z`.
    auto zi = builder.create<IntOp>({ new IntAttr(z) });
    auto incr = builder.create<AddIOp>({ loop, zi });
    opmap[loop] = incr;

    for (auto x : body)
      copy(x);

    // Rewire operands.
    for (auto [_, v] : opmap) {
      for (int i = 0; i < v->getOperandCount(); i++) {
        auto def = v->DEF(i);
        v->setOperand(i, opmap.count(def) ? opmap[def] : def);
      }
    }
    // This also changes `incr` itself. Restore it.
    incr->setOperand(0, loop);
  }

  // Now the step is `vi` times larger.
  auto stop = loop->DEF(1);
  auto step = loop->DEF(2);

  builder.setBeforeOp(loop);
  auto xstep = builder.create<IntOp>({ new IntAttr(V(step) * vi)});
  loop->setOperand(2, xstep);

  // Loop end should be truncated.
  auto li = builder.create<IntOp>({ new IntAttr(vi) });
  auto div = builder.create<DivIOp>({ stop, li });
  auto mul = builder.create<MulIOp>({ div, li });
  loop->setOperand(1, mul);

  // Create a side-loop from `mul` to `stop`.
  builder.setAfterOp(loop);
  auto ivAddr = loop->DEF(3);
  auto sideloop = builder.create<ForOp>({ mul, stop, step, ivAddr });
  auto sregion = sideloop->appendRegion();
  auto sEntry = sregion->appendBlock();

  // Copy the body to the sideloop.
  builder.setToBlockEnd(sEntry);
  opmap.clear();
  // Rename induction variable.
  opmap[loop] = sideloop;

  for (auto x : body)
    copy(x);

  // Rewire operands.
  for (auto [_, v] : opmap) {
    for (int i = 0; i < v->getOperandCount(); i++) {
      auto def = v->DEF(i);
      v->setOperand(i, opmap.count(def) ? opmap[def] : def);
    }
  }
}

void Unswitch::run() {
  auto loops = module->findAll<ForOp>();

  for (auto loop : loops) {
    // The step must be a constant.
    if (!isa<IntOp>(loop->DEF(2)))
      continue;

    // Find an "if" that is related to the induction variable.
    Op *branch = nullptr;
    auto region = loop->getRegion();
    auto entry = region->getFirstBlock();
    for (auto op : entry->getOps()) {
      if (isa<IfOp>(op)) {
        branch = op;
        break;
      }
    }
    if (!branch)
      continue;

    auto cond = branch->DEF();

    // Situation 1. comparison of a mod
    int vi;
    if (!cmpmod.match(cond, { { "x", loop } }))
      goto situation2;
    else
      vi = V(cmpmod.extract("'a"));

    vi = std::abs(vi);
    if (vi > 16 || vi <= 1)
      continue;

    // If `start` is not a constant,
    // we wouldn't be able to pre-calculate the mod value.
    if (!isa<IntOp>(loop->DEF(0)))
      continue;

    // Unroll the loop `vi` times.
    unroll(loop, vi);
    // Check `mod`s inside the loop and erase them when possible.
    tidymod(loop, vi);
    unswitched++;

    continue;
    // Situation 2. comparison of constant
    situation2:
      ; // TODO
  }
}
