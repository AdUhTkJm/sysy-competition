#!pnpm tsx

type Operator = string;
type Variable = string;
type Expr = 
  | { type: 'var', value: Variable }
  | { type: 'op', op: Operator, left: Expr, right: Expr };

const nodeIndex: { [k: number]: Expr[] } = {};
const variables: Variable[] = ['x', 'c'];
const operators: Operator[] = ['+', '-', '&', '|', '^'];

nodeIndex[0] = variables.map((v) => ({ type: 'var', value: v }));

const isc = (x: Expr): boolean => (x.type == 'var' && x.value == 'c')
const isx = (x: Expr): boolean => (x.type == 'var' && x.value == 'x')

function redundant(op: string, left: Expr, right: Expr) {
  return (
    // Foldable
    (isc(left) && isc(right)) ||
    // Constant
    (isx(left) && isx(right) && op != '+') ||
    // Commutative
    (isc(left) && isx(right) && ['&', '|', '^', '+'].includes(op))
  )
}

// Generate all possible expressions with exactly `n` internal nodes.
function nodes(n: number) {
  nodeIndex[n] = [];
  // For each possible split, distribute remaining nodes between left and right.
  for (let lnodes = 0; lnodes < n; lnodes++) {
    for (const op of operators) {
      for (const left of nodeIndex[lnodes]) {
        for (const right of nodeIndex[n - 1 - lnodes]) {
          if (redundant(op, left, right))
            continue;

          nodeIndex[n].push({ type: 'op', op, left, right });
        }
      }
    }
  }
}

function toReadableString(expr: Expr): string {
  if (expr.type == 'var')
    return expr.value;
  return `(${toReadableString(expr.left)} ${expr.op} ${toReadableString(expr.right)})`;
}

const opname = {
  "+": "Add",
  "-": "Sub",
  "|": "Or",
  "&": "And",
  "^": "Xor",
};

function toString(expr: Expr): string {
  let counter = 0;

  function helper(x: Expr) {
    if (x.type == 'var') {
      let varname = x.value == 'x' ? 'x' : `c${counter++}`;
      return `_${varname}`;
    }
    return `ctx.create(BvExpr::${opname[x.op]}, ${helper(x.left)}, ${helper(x.right)})`
  }

  return helper(expr);
}

for (let varname of ["x", "c0", "c1"])
  console.log(`  auto _${varname} = ctx.create(BvExpr::Var, "${varname}");`);

// Generate all expressions with 0 to maxNodes internal nodes.
for (let n = 1; n <= 2; n++) {
  nodes(n);
  for (let expr of nodeIndex[n])
    console.log(`  candidates.push_back(${toString(expr)});`)
}
