#include "Passes.h"
#include <unordered_set>

using namespace sys;

std::map<std::string, int> InlineStore::stats() {
  return {
    { "inlined-stores", inlined }
  };
}

#define BAD { bad = true; break; }

void InlineStore::run() {
  auto gets = module->findAll<GetGlobalOp>();
  auto gMap = getGlobalMap();
  auto fMap = getFunctionMap();

  // For each global, records in which functions they're used.
  std::unordered_map<std::string, std::unordered_set<std::string>> used;
  for (auto get : gets)
    used[NAME(get)].insert(NAME(get->getParentOp()));

  // Remove unused globals, and find out ones used in <once> functions.
  std::vector<std::string> queue;
  for (auto [k, v] : gMap) {
    if (used[k].empty())
      v->erase();
    if (used[k].size() == 1) {
      auto name = *used[k].begin();
      if (fMap[name]->has<AtMostOnceAttr>())
        queue.push_back(k);
    }
  }

  for (auto gname : queue) {
    auto funcname = *used[gname].begin();
    Op *func = fMap[funcname];
    Builder builder;

    auto region = func->getRegion();
    auto entry = region->getFirstBlock();
    auto glob = gMap[gname];
    bool fp = glob->has<FloatArrayAttr>();
    bool bad = false;

    for (auto runner = entry; runner->succs.size();) {
      auto ops = runner->getOps();
      for (auto op : ops) {
        if (isa<LoadOp>(op)) {
          if (!op->DEF()->has<AliasAttr>())
            BAD

          auto alias = ALIAS(op->DEF());
          if (alias->location.size() > 1)
            BAD

          auto [base, offsets] = *alias->location.begin();
          if (offsets.size() > 1 || offsets[0] == -1)
            BAD
          if (base != glob)
            BAD

          auto offset = offsets[0];
          builder.setBeforeOp(op);
          if (fp) {
            auto attr = glob->get<FloatArrayAttr>();
            auto f = builder.create<FloatOp>({ new FloatAttr(attr->vf[offset / 4]) });
            op->replaceAllUsesWith(f);
          } else {
            auto attr = glob->get<IntArrayAttr>();
            auto i = builder.create<IntOp>({ new IntAttr(attr->vi[offset / 4]) });
            op->replaceAllUsesWith(i);
          }
          op->erase();
        }

        if (isa<StoreOp>(op)) {
          if (!op->DEF(1)->has<AliasAttr>())
            BAD

          auto alias = ALIAS(op->DEF(1));
          if (alias->location.size() > 1)
            BAD

          auto [base, offsets] = *alias->location.begin();
          if (offsets.size() > 1 || offsets[0] == -1)
            BAD
          if (base != glob)
            BAD

          auto offset = offsets[0];
          if (fp) {
            if (!isa<FloatOp>(op->DEF(0)))
              continue;

            auto attr = glob->get<FloatArrayAttr>();
            attr->vf[offset / 4] = F(op->DEF(0));
          } else {
            if (!isa<IntOp>(op->DEF(0)))
              continue;

            auto attr = glob->get<IntArrayAttr>();
            attr->vi[offset / 4] = V(op->DEF(0));
          }
          op->erase();
        }
      }
      
      if (bad)
        break;

      auto term = runner->getLastOp();
      if (isa<GotoOp>(term) && TARGET(term)->preds.size() == 1)
        runner = TARGET(term);
      else break;
    }

    // Update allzero attribute.
    if (fp) {
      auto attr = glob->get<FloatArrayAttr>();
      for (int i = 0; i < attr->size; i++) {
        if (attr->vf[i] != 0) {
          attr->allZero = false;
          break;
        }
      }
    } else {
      auto attr = glob->get<IntArrayAttr>();
      for (int i = 0; i < attr->size; i++) {
        if (attr->vi[i] != 0) {
          attr->allZero = false;
          break;
        }
      }
    }
  }
}
