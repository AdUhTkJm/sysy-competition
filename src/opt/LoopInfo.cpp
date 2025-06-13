#include "LoopPasses.h"

using namespace sys;

void LoopInfo::dump(std::ostream &os) {
  os << "Blocks: ";
  for (auto bb : bbs)
    os << bbmap[bb] << " ";
  os << "\n";

  os << "Preheader: " << (preheader ? std::to_string(bbmap[preheader]) : "none") << "\n";
  os << "Header: " << bbmap[header] << "\n";

  os << "Exits: ";
  for (auto bb : exits)
    os << bbmap[bb] << " ";
  os << "\n";

  os << "Latches: ";
  for (auto bb : latches)
    os << bbmap[bb] << " ";
  os << "\n";
}

void LoopForest::dump(std::ostream &os) {
  for (auto loop : loops) {
    loop->dump(os);
    os << "\n\n";
  }
}
