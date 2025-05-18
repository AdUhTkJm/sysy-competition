#include "SMT.h"

using namespace smt;

bool UnionFind::has(Variable x) {
  return parent.count(x);
}

Variable UnionFind::find(Variable x) {
  if (!has(x)) {
    parent[x] = x;
    rank[x] = 0;
    return x;
  }
  if (parent[x] != x)
    parent[x] = find(parent[x]);
  return parent[x];
}

void UnionFind::link(Variable x, Variable y) {
  Variable xr = find(x);
  Variable yr = find(y);
  if (xr == yr)
    return;
  if (rank[xr] < rank[yr])
    parent[xr] = yr;
  else if (rank[xr] > rank[yr])
    parent[yr] = xr;
  else {
    parent[yr] = xr;
    rank[xr]++;
  }
}

bool UnionFind::equiv(Variable x, Variable y) {
  return find(x) == find(y);
}
