#ifndef PASS_MANAGER_H
#define PASS_MANAGER_H

#include "Pass.h"
#include "../main/Options.h"

namespace sys {

class PassManager {
  std::vector<Pass*> passes;
  ModuleOp *module;

  bool pastFlatten;
  bool pastMem2Reg;
  bool inBackend;
  int exitcode;
  
  std::string input;
  std::string truth;

  Options opts;
public:
  PassManager(ModuleOp *module, const Options &opts);
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
