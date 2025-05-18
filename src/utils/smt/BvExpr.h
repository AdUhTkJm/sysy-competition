#ifndef BVEXPR_H
#define BVEXPR_H

#include <string>
#include <iostream>
#include <unordered_set>

namespace smt {

#define TYPES \
  X(Var) X(Const) X(And) X(Or) X(Xor) X(Not) X(Add) X(Eq)

class BvExpr {
public:
  #define X(x) x, 
  enum Type {
    TYPES
  } ty;
  #undef X

  #define X(x) #x, 
  static constexpr const char *names[] = {
    TYPES
  };
  #undef X

  BvExpr *l = nullptr, *r = nullptr;
  int vi = 0;
  std::string name;

  BvExpr(Type ty): ty(ty) {}
  BvExpr(Type ty, BvExpr *l): ty(ty), l(l) {}
  BvExpr(Type ty, BvExpr *l, BvExpr *r): ty(ty), l(l), r(r) {}
  BvExpr(const std::string &name): ty(Var), name(name) {}

  std::string dump(std::ostream &os = std::cerr);
};

#undef TYPES

class BvExprContext {
  struct Eq {
    bool operator()(BvExpr *a, BvExpr *b) const {
      return a->ty == b->ty && a->l == b->l && a->r == b->r && a->name == b->name && a->vi == b->vi;
    }
  };

  struct Hash {
    // From boost::hash_combine.
    static void hash_combine(size_t &a, size_t b) {
      a ^= b + 0x9e3779b9 + (a << 6) + (a >> 2);
    }

    size_t operator()(BvExpr *a) const {
      size_t result = a->ty;
      hash_combine(result, a->vi);
      if (a->l)
        hash_combine(result, Hash()(a->l));
      if (a->r)
        hash_combine(result, Hash()(a->r));
      if (a->ty == BvExpr::Var)
        hash_combine(result, std::hash<std::string>()(a->name));
      return result;
    }
  };

  std::unordered_set<BvExpr*, Hash, Eq> set;
public:
  template<class T, class... Args>
  BvExpr *create(BvExpr::Type ty, Args... args) {
    BvExpr *p = new BvExpr(ty, args...);
    if (auto it = set.find(p); it != set.end()) {
      delete p;
      return *it;
    }
    set.insert(p);
    return p;
  }

  ~BvExprContext() {
    for (auto x : set)
      delete x;
  }
};

}

#endif
