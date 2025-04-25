#ifndef TYPE_CONTEXT_H
#define TYPE_CONTEXT_H

#include <unordered_set>

#include "Type.h"
#include "../utils/DynamicCast.h"

namespace sys {

// Manages memory of types.
class TypeContext {
  struct Hash {
    size_t operator()(Type *ty) const {
      size_t hash = ty->getID();

      if (auto arr = dyn_cast<ArrayType>(ty)) {
        hash = (hash << 4) + Hash()(arr->base);
        for (auto x : arr->dims)
          hash *= (x + 1);
      }

      if (auto ptr = dyn_cast<PointerType>(ty)) {
        hash = (hash << 4) + Hash()(ptr->pointee);
      }

      if (auto fn = dyn_cast<FunctionType>(ty)) {
        hash = (hash << 4) + Hash()(fn->ret);
        for (auto x : fn->params) {
          hash <<= 1;
          hash += Hash()(x);
        }
      }

      return hash;
    }
  };

  struct Eq {
    bool operator()(Type *a, Type *b) const {
      if (a->getID() != b->getID())
        return false;

      if (auto arr = dyn_cast<ArrayType>(a)) {
        auto arrb = cast<ArrayType>(b);
        if (arr->dims.size() != arrb->dims.size())
          return false;

        for (int i = 0; i < arr->dims.size(); i++) {
          if (arr->dims[i] != arrb->dims[i])
            return false;
        }

        return Eq()(arr->base, arrb->base);
      }

      if (auto ptr = dyn_cast<PointerType>(a)) {
        auto ptrb = cast<PointerType>(b);
        return Eq()(ptr->pointee, ptrb->pointee);
      }

      if (auto fn = dyn_cast<FunctionType>(a)) {
        auto fnb = cast<FunctionType>(b);
        if (fn->params.size() != fnb->params.size())
          return false;

        for (int i = 0; i < fn->params.size(); i++) {
          if (!Eq()(fn->params[i], fnb->params[i]))
            return false;
        }

        return Eq()(fn->ret, fnb->ret);
      }

      return true;
    }
  };

  std::unordered_set<Type*, Hash, Eq> content;
public:
  template<class T, class... Args>
  T *create(Args... args) {
    auto ptr = new T(std::forward<Args>(args)...);
    if (auto [it, absent] = content.insert(ptr); !absent) {
      delete ptr;
      return cast<T>(*it);
    }
    return ptr;
  }
};
  
}

#endif
