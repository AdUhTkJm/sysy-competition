#include "Attrs.h"
#include <sstream>

using namespace sys;

std::string TargetAttr::toString() {
  if (!bbmap.count(bb))
    bbmap[bb] = bbid++;
  return "<bb" + std::to_string(bbmap[bb]) + ">";
}

std::string ElseAttr::toString() {
  if (!bbmap.count(bb))
    bbmap[bb] = bbid++;
  return "<else = bb" + std::to_string(bbmap[bb]) + ">";
}

std::string FromAttr::toString() {
  if (!bbmap.count(bb))
    bbmap[bb] = bbid++;
  return "<from = bb" + std::to_string(bbmap[bb]) + ">";
}

IntArrayAttr::IntArrayAttr(int *vi, int size): vi(vi), size(size), allZero(true) {
  for (int i = 0; i < size; i++) {
    if (vi[i] != 0) {
      allZero = false;
      break;
    }
  }
}

std::string IntArrayAttr::toString() {
  if (allZero)
    return "<array = 0 x " + std::to_string(size) + ">";
  
  std::stringstream ss;
  ss << "<array = ";
  if (size > 0)
    ss << vi[0];
  for (int i = 1; i < size; i++)
    ss << ", " << vi[i];
  ss << ">";
  return ss.str();
}

std::string CallerAttr::toString() {
  std::stringstream ss;
  ss << "<caller = ";
  if (callers.size() > 0)
    ss << callers[0];
  for (int i = 1; i < callers.size(); i++)
    ss << ", " << callers[i];
  ss << ">";
  return ss.str();
}
