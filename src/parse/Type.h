#ifndef TYPE_H
#define TYPE_H

#include <string>
#include <vector>
namespace sys {

class Type {
  int id;
public:
  int getID() const { return id; }
  virtual std::string toString() const = 0;

  Type(int id): id(id) {}
};

template<class T, int TypeID>
class TypeImpl : public Type {
public:
  static bool classof(Type *ty) {
    return ty->getID() == TypeID;
  }

  TypeImpl(): Type(TypeID) {}
};

class IntType : public TypeImpl<IntType, __LINE__> {
public:
  std::string toString() const override { return "int"; }
};

class FloatType : public TypeImpl<FloatType, __LINE__> {
public:
  std::string toString() const override { return "float"; }
};

class VoidType : public TypeImpl<VoidType, __LINE__> {
public:
  std::string toString() const override { return "void"; }
};

class PointerType : public TypeImpl<PointerType, __LINE__> {
public:
  Type *pointee;

  PointerType(Type *pointee): pointee(pointee) {}
  
  std::string toString() const override { return pointee->toString() + "*"; }
};

class FunctionType : public TypeImpl<FunctionType, __LINE__> {
public:
  Type *ret;
  std::vector<Type*> params;

  FunctionType(Type *ret, std::vector<Type*> params):
    ret(ret), params(params) {}

  std::string toString() const override;
};

class ASTNode;
class ArrayType : public TypeImpl<ArrayType, __LINE__> {
public:
  Type *base;
  // We will fold them.
  std::vector<ASTNode*> dimExprs;

  std::vector<int> dims;

  ArrayType(Type *base, std::vector<ASTNode*> dimExprs):
    base(base), dimExprs(dimExprs) {}

  std::string toString() const override;
};

}

#endif
