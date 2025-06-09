#ifndef BASIC_SET_H
#define BASIC_SET_H

#include <vector>
#include <iostream>

namespace pres {

using AffineExpr = std::vector<int>;

class BasicSet {
  // [A I -b] [x 1]^T = 0
  std::vector<AffineExpr> tableau;
  std::vector<int> denom;
public:
  BasicSet(const std::vector<AffineExpr> &tableau): tableau(tableau) {}
  void dump(std::ostream &os);

  bool empty();
};

}

#endif
