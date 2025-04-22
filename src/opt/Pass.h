#ifndef PASS_H
#define PASS_H

#include <map>
#include <string>
#include <vector>

#include "../codegen/Ops.h"

namespace sys {

class Pass {
  ModuleOp *module;
public:
  Pass(ModuleOp *module): module(module) {}
  virtual ~Pass() {}
  virtual std::string name() = 0;
  virtual std::map<std::string, int> stats() = 0;
  virtual void run() = 0;
};

template<class T>
class Rewriter {
public:
  virtual void rewrite(T *op) = 0;
  void run(Op *op);
};

class PassManager {
  std::vector<Pass*> passes;
  ModuleOp *module;
public:
  PassManager(ModuleOp *module): module(module) {}
  ~PassManager();

  void run();

  template<class T, class... Args>
  void addPass(Args... args) {
    auto pass = new T(module, std::forward<Args>(args)...);
    passes.push_back(pass);
  }
};

}

#endif
