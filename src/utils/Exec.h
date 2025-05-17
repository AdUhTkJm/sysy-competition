#ifndef EXEC_H
#define EXEC_H

#include "../codegen/Ops.h"
#include <sstream>
#include <map>
#include <unordered_map>

namespace sys::exec {

class Interpreter {
  union Value {
    int vi;
    float vf;
    int *vp;
    float *vfp;
  };

  using SymbolTable = std::unordered_map<Op*, Value>;

  std::stringstream buffer;
  std::map<std::string, Op*> fnMap;
  std::map<std::string, Value> globalMap;

  SymbolTable value;
  // Instruction pointer.
  Op *ip;

  int eval(Op *op);
  float evalf(Op *op);
  int *evalp(Op *op);
  float *evalfp(Op *op);

  void exec(Op *op);
  Value execf(Region *region);

  int retcode = 0;

  struct SemanticScope {
    Interpreter &parent;
    SymbolTable table;
  public:
    SemanticScope(Interpreter &itp): parent(itp), table(itp.value) {}
    ~SemanticScope() { parent.value = table; }
  };
public:
  Interpreter(ModuleOp *module);
  ~Interpreter();

  void run(std::istream &input);
  std::string out() { return buffer.str(); }
  int exitcode() { return retcode; }
};

}

#endif
