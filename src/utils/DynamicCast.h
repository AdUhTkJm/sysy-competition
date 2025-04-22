#ifndef DYNAMIC_CAST_H
#define DYNAMIC_CAST_H

#include <cassert>

namespace sys {

template<class T, class U>
bool isa(U *t) {
  return T::classof(t);
}

template<class T, class U>
T *cast(U *t) {
  assert(isa<T>(t));
  return (T*) t;
}

template<class T, class U>
T *dyn_cast(U *t) {
  if (!isa<T>(t))
    return nullptr;
  return cast<T>(t);
}

}

#endif
