#include "CleanupPasses.h"

using namespace sys;

struct Associated {
  bool ref;
  std::vector<Op*> mem;
};

void Reassociate::runImpl(Region *region) {
  std::map<Op*, Associated> data;
  auto domtree = getDomTree(region);

  std::vector<BasicBlock*> queue { region->getFirstBlock() };
  while (!queue.empty()) {
    auto bb = queue.back();
    queue.pop_back();

    for (auto op : bb->getOps()) {
      if (!isa<AddIOp>(op))
        continue;

      auto x = op->DEF(0), y = op->DEF(1);
      std::vector<Op*> mem;
      
      if (data.count(x))
        std::copy(data[x].mem.begin(), data[x].mem.end(), std::back_inserter(mem)),
        data[x].ref = true;
      else
        mem.push_back(x);
      
      if (data.count(y))
        std::copy(data[y].mem.begin(), data[y].mem.end(), std::back_inserter(mem)),
        data[y].ref = true;
      else
        mem.push_back(y);

      data[op] = { false, mem };
    }

    for (auto child : domtree[bb])
      queue.push_back(child);
  }

  for (auto [k, v] : data) {
    if (v.ref || v.mem.size() == 2)
      continue;

    // We require every addition is used only once.
    auto mem = v.mem;
    bool good = true;
    for (auto op : mem) {
      if (op->getUses().size() > 1) {
        good = false;
        break;
      }
    }
    if (!good)
      continue;

    // Reassociate.
    std::vector<Op*> copy;
    Builder builder;
    while (mem.size() != 1) {
      builder.setBeforeOp(k);
      for (int i = 0; i + 1 < mem.size(); i += 2) {
        auto add = builder.create<AddIOp>({ mem[i]->getResult(), mem[i + 1] });
        copy.push_back(add);
      }
      if (mem.size() & 1)
        copy.push_back(mem.back());
      mem = copy;
      copy.clear();
    }
    auto fulladd = mem[0];
    k->replaceAllUsesWith(fulladd);
    k->erase();
  }
}

void Reassociate::run() {
  auto funcs = collectFuncs();

  for (auto func : funcs)
    runImpl(func->getRegion());
}
