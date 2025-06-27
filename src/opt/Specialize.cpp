#include "Passes.h"
#include "Analysis.h"
#include "CleanupPasses.h"

using namespace sys;

// NOTE: we consider 0 as both positive and negative here.

namespace {

// E.g. __pos_1_fib means argument 1 (second) is specialized to be positive.
std::string posname(const std::string &name, int i) {
  return "__pos_" + std::to_string(i) + "_" + name;;
}

void copy(Region *tgt, Region *src) {
  std::unordered_map<BasicBlock*, BasicBlock*> rewireMap;
  std::unordered_map<Op*, Op*> cloneMap;
  Builder builder;

  for (auto x : src->getBlocks())
    rewireMap[x] = tgt->appendBlock();

  for (auto [k, v] : rewireMap) {
    builder.setToBlockStart(v);
    for (auto op : k->getOps())
      cloneMap[op] = builder.copy(op);
  }

  // Rewire operands.
  for (auto [_, v] : cloneMap) {
    for (int i = 0; i < v->getOperandCount(); i++)
      v->setOperand(i, cloneMap[v->DEF(i)]);
  }

  // Rewire basic blocks.
  for (auto [_, v] : rewireMap) {
    auto term = v->getLastOp();
    if (auto target = term->find<TargetAttr>(); target && target->bb)
      target->bb = rewireMap[target->bb];
    if (auto ifnot = term->find<ElseAttr>(); ifnot && ifnot->bb)
      ifnot->bb = rewireMap[ifnot->bb];
  }

  // Rewire phis.
  for (auto [_, v] : cloneMap) {
    if (!isa<PhiOp>(v))
      continue;

    // RangeAttr isn't deleted yet.
    for (auto attr : v->getAttrs()) {
      if (!isa<FromAttr>(attr))
        continue;
      auto &from = FROM(attr);
      from = rewireMap[from];
    }
  }
}

std::map<std::string, std::set<int>> produced;
std::set<std::string> blacklist;

}

bool Specialize::specialize() {
  auto calls = module->findAll<CallOp>();
  std::unordered_map<std::string, std::set<int>> posinfo;

  for (auto call : calls) {
    auto &name = NAME(call);
    if (isExtern(name) || produced.count(name) || blacklist.count(name))
      continue;
    
    for (int i = 0; i < call->getOperandCount(); i++) {
      auto def = call->DEF(i);
      if (!def->has<RangeAttr>())
        continue;
      auto [low, high] = RANGE(def);
      if (low >= 0) {
        posinfo[name].insert(i);
        name = posname(name, i);
        blacklist.insert(name);
        break;
      }

      // TODO: negative
      // TODO: perhaps more possible ranges?
    }
  }

  auto fmap = getFunctionMap();
  Builder builder;

  bool changed = false;
  for (auto [name, info] : posinfo) {
    if (blacklist.count(name))
      continue;

    for (auto i : info) {
      if (produced[name].count(i))
        continue;

      changed = true;
      auto fn = fmap[name];
      builder.setToRegionStart(module->getRegion());
      auto copied = builder.create<FuncOp>({
        new NameAttr(posname(name, i)),
        fn->get<ArgCountAttr>()
      });
      copied->appendRegion();

      copy(copied->getRegion(), fn->getRegion());
    }
  }

  Range(module).run();
  RangeAwareFold(module).run();
  return changed;
}

void Specialize::run() {
  Range(module).run();
  while (specialize());
  CallGraph(module).run();
}
