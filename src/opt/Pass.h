#ifndef PASS_H
#define PASS_H

#include <map>
#include <string>
#include <type_traits>
#include <vector>
#include <iostream>

#include "../codegen/Ops.h"

namespace sys {

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
    do {
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
  std::map<std::string, FuncOp*> getFunctionMap();
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
public:
  PassManager(ModuleOp *module): module(module) {}
  ~PassManager();

  void setVerbose(bool verbose);
  void setPrintStats(bool print);

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
