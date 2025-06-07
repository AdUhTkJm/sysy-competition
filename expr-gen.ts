#!pnpm tsx

type Operator = string;
type Variable = string;
type Expr = 
  | { type: 'var', value: Variable }
  | { type: '1', op: Operator, node: Expr }
  | { type: '2', op: Operator, left: Expr, right: Expr }
  | { type: '3', op: Operator, cond: Expr, left: Expr, right: Expr };

const nodeIndex: { [k: number]: Expr[] } = {};
const variables: Variable[] = ['x', 'y', 'c'];
const unary: Operator[] = ['-u', '~'];
const binary: Operator[] = ['+', '-', '&', '|', '^', '*', '/', '%'];
const ternary: Operator[] = ['*%', '?:'];

const isc = (x: Expr): boolean => (x.type == 'var' && x.value == 'c')
const isx = (x: Expr): boolean => (x.type == 'var' && x.value != 'c')
const isa = (x: Expr, op: Operator): boolean => (x.type != 'var' && x.op == op)

function redundant(expr: Expr) {
  if (expr.type == '1') {
    const op = expr.op;
    const node = expr.node;

    return (
      (op == '-u' && (isa(node, '-u') || isa(node, '-') || isa(node, '+'))) ||
      (op == '~' && isa(node, '~')) ||
      (isc(node))
    )
  }
  if (expr.type == '2') {
    const op = expr.op;
    const left = expr.left;
    const right = expr.right;

    return (
      // Foldable
      (isc(left) && isc(right)) ||
      // Constant
      (isx(left) && isx(right) && !(op == '+' || op == '*')) ||
      // Commutative
      (left.type == 'var' && right.type == 'var' && (isc(left) && isx(right) || left.value > right.value)
       && ['&', '|', '^', '+', '*'].includes(op)) ||
      // Combinable
      (op == "%" && (isa(left, "*") || isa(right, '-u')))
    )
  }
  if (expr.type == '3') {
    const op = expr.op;
    const cond = expr.cond;
    const left = expr.left;
    const right = expr.right;

    // Both *% and ?: will regard these as redundant.
    return (
      isc(cond) || left == right ||
      (op == '*%' && cond == right) ||
      (op == '*%' && isa(right, '-u')) ||
      (op == '*%' && cond.type == 'var' && left.type == 'var' && cond.value > left.value) ||
      (op == '*%' && isc(left) && isc(right)) ||
      (op == '?:' && cond.type == '3' && cond.op == '?:' && cond.cond == cond)
    );
  }
  return false;
}

nodeIndex[0] = variables.map((v) => ({ type: 'var', value: v }));

// Create nodeIndex[1].
nodeIndex[1] = [];
for (const op of unary) {
  for (const node of nodeIndex[0])
    nodeIndex[1].push({ type: '1', op: op, node: node });
}

for (const op of binary) {
  for (const node of nodeIndex[0]) {
    for (const node2 of nodeIndex[0])
      nodeIndex[1].push({ type: '2', op: op, left: node, right: node2 });
  }
}

for (const op of ternary) {
  for (const node of nodeIndex[0]) {
    for (const node2 of nodeIndex[0]) {
      for (const node3 of nodeIndex[0])
        nodeIndex[1].push({ type: '3', op: op, cond: node, left: node2, right: node3 });
    }
  }
}

nodeIndex[1] = nodeIndex[1].filter((x) => !redundant(x))

// Create nodeIndex[2].
nodeIndex[2] = [];
for (const op of unary) {
  for (const node of nodeIndex[1])
    nodeIndex[2].push({ type: '1', op: op, node: node });
}

for (const op of binary) {
  for (const node of nodeIndex[1]) {
    for (const node2 of nodeIndex[0])
      nodeIndex[2].push({ type: '2', op: op, left: node, right: node2 });
  }
  
  for (const node of nodeIndex[0]) {
    for (const node2 of nodeIndex[1])
      nodeIndex[2].push({ type: '2', op: op, left: node, right: node2 });
  }
}

for (const op of ternary) {
  for (const node of nodeIndex[1]) {
    for (const node2 of nodeIndex[0]) {
      for (const node3 of nodeIndex[0])
        nodeIndex[2].push({ type: '3', op: op, cond: node, left: node2, right: node3 });
    }
  }

  for (const node of nodeIndex[0]) {
    for (const node2 of nodeIndex[1]) {
      for (const node3 of nodeIndex[0])
        nodeIndex[2].push({ type: '3', op: op, cond: node, left: node2, right: node3 });
    }
  }
  
  for (const node of nodeIndex[0]) {
    for (const node2 of nodeIndex[0]) {
      for (const node3 of nodeIndex[1])
        nodeIndex[2].push({ type: '3', op: op, cond: node, left: node2, right: node3 });
    }
  }
}

nodeIndex[2] = nodeIndex[2].filter((x) => !redundant(x))

const opname = {
  "-u": "Minus",
  "~": "Not",
  "+": "Add",
  "-": "Sub",
  "*": "Mul",
  "/": "Div",
  "%": "Mod",
  "|": "Or",
  "&": "And",
  "^": "Xor",
  "?:": "Ite",
  "*%": "MulMod",
};

function toString(expr: Expr): string {
  let counter = 0;

  function helper(x: Expr) {
    if (x.type == 'var') {
      let varname = x.value != 'c' ? x.value : `c${counter++}`;
      return `_${varname}`;
    }

    if (x.type == '1')
      return `ctx.create(BvExpr::${opname[x.op]}, ${helper(x.node)})`
    if (x.type == '2')
      return `ctx.create(BvExpr::${opname[x.op]}, ${helper(x.left)}, ${helper(x.right)})`
    if (x.type == '3')
      return `ctx.create(BvExpr::${opname[x.op]}, ${helper(x.cond)}, ${helper(x.left)}, ${helper(x.right)})`
  }

  return helper(expr);
}

console.log('#include "../utils/smt/BvExpr.h"');
console.log("#include <vector>");
console.log("using namespace smt;\n")
console.log(`inline void tsgen_push_${variables.length - 1}(BvExprContext &ctx, std::vector<BvExpr*> &candidates) {`);

let total = nodeIndex[0].length + nodeIndex[1].length + nodeIndex[2].length;
console.log(`  candidates.reserve(${total});`);

for (let varname of [...variables, "c0", "c1"]) {
  if (varname != 'c')
    console.log(`  auto _${varname} = ctx.create(BvExpr::Var, "${varname}");`);
}

// Generate all expressions with 0 to maxNodes internal nodes.
for (let n = 1; n <= 2; n++) {
  for (let expr of nodeIndex[n])
    console.log(`  candidates.push_back(${toString(expr)});`)
}
console.log('}');