#ifndef EXEC_H
#define EXEC_H

#include "../codegen/Ops.h"
#include <cstdint>
#include <sstream>
#include <map>
#include <unordered_map>

namespace sys::exec {

class Interpreter {
  union Value {
    intptr_t vi;
    float vf;
  };

  using SymbolTable = std::unordered_map<Op*, Value>;

  std::stringstream outbuf, inbuf;
  std::map<std::string, Op*> fnMap;
  std::set<std::string> fpGlobals;
  std::map<std::string, Value> globalMap;

  SymbolTable value;
  // Used for phi functions.
  BasicBlock *prev;
  // Instruction pointer.
  Op *ip;

  intptr_t eval(Op *op);
  float evalf(Op *op);

  void store(Op *op, float v);
  void store(Op *op, intptr_t v);

  void exec(Op *op);
  Value execf(Region *region, const std::vector<Value> &args);

  Value applyExtern(const std::string &name, const std::vector<Value> &args);

  unsigned retcode;

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
  std::string out() { return outbuf.str(); }
  int exitcode() { return retcode & 0xff; }
};

}

#endif
