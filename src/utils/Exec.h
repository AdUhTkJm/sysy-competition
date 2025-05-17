#ifndef EXEC_H
#define EXEC_H

#include "../codegen/Ops.h"
#include <sstream>
#include <map>

namespace sys::exec {

class Interpreter {
  std::stringstream buffer;
  std::map<std::string, Op*> fnMap;

  void execf(Region *region);
public:
  Interpreter(ModuleOp *module) {}

  std::string run(std::istream &input);
};

}

#endif
