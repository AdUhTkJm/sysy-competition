#include <iostream>
#include <fstream>

#include "ArmPasses.h"

using namespace sys;
using namespace sys::arm;

void Dump::dumpBody(Region *region, std::ostream &os) {
  
}

void Dump::dump(std::ostream &os) {
  os << ".global main";

  auto funcs = collectFuncs();
  for (auto func : funcs) {
    os << NAME(func) << ":\n";
    dumpBody(func->getRegion(), os);
    os << "\n\n";
  }
}

void Dump::run() {
  if (out.size() != 0) {
    std::ofstream ofs(out);
    dump(ofs);
  } else
    dump(std::cout);
}
