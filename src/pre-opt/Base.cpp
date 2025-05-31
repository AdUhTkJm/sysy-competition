#include "PreAnalysis.h"
#include "PreAttrs.h"
#include "../codegen/CodeGen.h"

using namespace sys;

void Base::runImpl(Region *region) {
  for (auto bb : region->getBlocks()) {
    for (auto op : bb->getOps()) {
      // Recursively deal with inner regions.
      for (auto r : op->getRegions())
        runImpl(r);
      
      // The base of an alloca/getglobal is itself.
      if (isa<AllocaOp>(op) || isa<GetGlobalOp>(op)) {
        op->add<BaseAttr>(op);
        continue;
      }
      
      // For addl, the base is that of the address.
      if (isa<AddLOp>(op)) {
        auto x = op->DEF(0);
        auto y = op->DEF(1);
        if (!x->has<BaseAttr>()) {
          if (y->has<BaseAttr>())
            std::swap(x, y);
          else continue;
        }

        op->add<BaseAttr>(BASE(x));
        continue;
      }
    }
  }
}

void Base::run() {
  auto funcs = collectFuncs();
  
  for (auto func : funcs) {
    // Find the place to insert in.
    Region *region = func->getRegion();
    auto bb = region->getFirstBlock();
    if (bb->getOpCount() && isa<AllocaOp>(bb->getFirstOp()))
      bb = bb->nextBlock();

    // Hoist GetGlobalOp to the front.
    auto gets = func->findAll<GetGlobalOp>();
    std::unordered_map<std::string, Op*> hoisted;
    Builder builder;

    for (auto get : gets) {
      const auto &name = NAME(get);
      if (!hoisted.count(name)) {
        builder.setToBlockStart(bb);
        auto newget = builder.create<GetGlobalOp>({ new NameAttr(name) });
        hoisted[name] = newget;
      }
      get->replaceAllUsesWith(hoisted[name]);
      get->erase();
    }
    
    runImpl(region);
  }
}
