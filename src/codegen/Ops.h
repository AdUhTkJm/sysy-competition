#ifndef OPS_H
#define OPS_H

#include "OpBase.h"

#define DEFOP(Ty, name) \
  class Ty : public OpImpl<Ty, __LINE__, name>

namespace sys {


}

#endif
