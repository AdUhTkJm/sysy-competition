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
        success |= rewriter(t);
    } while (success);
  }

  template<class F>
  void runRewriter(F rewriter) {
    runRewriter(module, rewriter);
  }

  static bool isExtern(const std::string &name);

  // This will be faster than module->findAll<FuncOp>,
  // as it doesn't need to iterate through the contents of functions.
  std::vector<FuncOp*> collectFuncs();
  // Same as above, only that it's for global variables.
  std::vector<GlobalOp*> collectGlobals();
  std::map<std::string, FuncOp*> getFunctionMap();
  std::map<std::string, GlobalOp*> getGlobalMap();
  DomTree getDomTree(Region *region);
public:
  Pass(ModuleOp *module): module(module) {}
  virtual ~Pass() {}
  virtual std::string name() = 0;
  virtual std::map<std::string, int> stats() = 0;
  virtual void run() = 0;
};

class PassManager {
  std::vector<Pass*> passes;
  ModuleOp *module;

  bool verbose = false;
  bool print = false;
  bool verify = false;
  std::string printAfter;
public:
  PassManager(ModuleOp *module): module(module) {}
  ~PassManager();

  void setVerbose(bool verbose);
  void setPrintStats(bool print);
  void setPrintAfter(const std::string &passName);
  void setVerify(bool verify);

  void run();
  ModuleOp *getModule() { return module; }

  template<class T, class... Args>
  void addPass(Args... args) {
    auto pass = new T(module, std::forward<Args>(args)...);
    passes.push_back(pass);
  }
};

}

#endif
