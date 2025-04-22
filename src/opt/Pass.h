#ifndef PASS_H
#define PASS_H

#include <map>
#include <string>
#include <vector>

#include "../codegen/Ops.h"

namespace sys {

class Pass {
protected:
  ModuleOp *module;

  template<class T>
  void runRewriter(T rewriter) {
    auto ts = module->findAll<T>();
    bool success;
    do {
      success = false;
      for (auto t : ts)
        success |= rewriter(t);
    } while (success);
  }
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
public:
  PassManager(ModuleOp *module): module(module) {}
  ~PassManager();

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
