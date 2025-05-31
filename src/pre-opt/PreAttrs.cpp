#include "PreAttrs.h"
#include <sstream>

using namespace sys;

std::string SubscriptAttr::toString() {
  std::stringstream ss;
  ss << "<subscript = ";
  if (subscript.size() > 0)
    ss << subscript[0];
  for (int i = 1; i < subscript.size(); i++)
    ss << ", " << subscript[i];
  ss << ">";
  return ss.str();
}
