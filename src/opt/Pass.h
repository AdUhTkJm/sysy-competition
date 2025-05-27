#ifndef PASS_H
#define PASS_H

#include <map>
#include <string>
#include <type_traits>
#include <vector>
#include <iostream>
#include <unordered_map>

#include "../codegen/Ops.h"

namespace sys {
  
using DomTree = std::unordered_map<BasicBlock*, std::vector<BasicBlock*>>;

bool isExtern(const std::string &name);

class Pass {
  template<typename F, typename Ret, typename A>
  static A helper(Ret (F::*)(A) const);

  template<class F>
  using argument_t = decltype(helper(&F::operator()));
protected:
  ModuleOp *module;

  template<class F>
  void runRewriter(Op *op, F rewriter) {
    using T = std::remove_pointer_t<argument_t<F>>;
    
    bool success;
    int total = 0;
    do {
      // Probably hit an infinite loop.
      if (++total > 10000)
        assert(false);
      
      auto ts = op->findAll<T>();
      success = false;
      for (auto t : ts)
        success |= rewriter(cast<T>(t));
    } while (success);
  }

  template<class F>
  void runRewriter(F rewriter) {
    runRewriter(module, rewriter);
  }

  // This will be faster than module->findAll<FuncOp>,
  // as it doesn't need to iterate through the contents of functions.
  std::vector<FuncOp*> collectFuncs();
  // Same as above, only that it's for global variables.
  std::vector<GlobalOp*> collectGlobals();
  std::map<std::string, FuncOp*> getFunctionMap();
  std::map<std::string, GlobalOp*> getGlobalMap();
  DomTree getDomTree(Region *region);

  // Find the first op that isn't an AllocaOp.
  Op *nonalloca(Region *region);
public:
  Pass(ModuleOp *module): module(module) {}
  void cleanup();
  virtual ~Pass() {}
  virtual std::string name() = 0;
  virtual std::map<std::string, int> stats() = 0;
  virtual void run() = 0;
};

}

#endif
