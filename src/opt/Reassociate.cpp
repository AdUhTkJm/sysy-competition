#include "CleanupPasses.h"

using namespace sys;

namespace {

struct Associated {
  bool ref;
  std::vector<Op*> mem;
};

}

void Reassociate::run() {
  
}
