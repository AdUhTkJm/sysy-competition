#include "Passes.h"

using namespace sys;

std::map<std::string, int> DCE::stats() {
  return {
    { "eliminated-ops", elim }
  };
}

void DCE::run() {

}
