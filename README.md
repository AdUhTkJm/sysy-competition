# 概述

这个编译器受到了不少 MLIR 的启发。这里的 IR 就是模仿它设计的。

每个 Op 都有恰好一个返回值，不定数量的操作数（`Value`，对 `Op*` 的一层包装），一些子作用域 (`Region*`，实际上是基本块的容器)，以及一些属性 (`Attr*`)。Op 本身并不对任何东西进行检查，但 Pass 会假定某些 Op 具有特定的操作个数和属性（例如 `AddIOp` 有恰好两个操作数）。

这样会让 Lowering 略微方便一些：比起将它转译为某种类似 MCInst 的东西，我可以直接利用已有的 Op 设施。不过我并不确定这是否是个好主意——或许使用 MCInst 反而会方便一些？

# 编译器结构

## 子工程

### SMT 求解器

在 `utils/smt` 中，我手工制作了一个简单的 SMT 求解器，可以求解不含量词的 bitvector 理论（QF_BV）。换言之，它可以判定两个涉及 32 位整数的算式是否相等，在相等时给出证明，不等时给出反例。

首先，我实现了一个基于 CDCL 算法的 SAT 求解器。接下来，对于 32 位整数，只需要将其拆成 32 个独立的布尔变量，按照数字电路的方式组合，就可以实现基本的运算。这意味着这个 SMT 求解器几乎没有做任何优化，执行效率较低。好在 CDCL 非常迅速，可以轻松解决数千个子句的问题，而且编译器内调用它的次数有限，问题不大。

## 本体

编译器

### Parser

Lexer 和 Parser 是手写的，其中 Parser 是简单的递归下降。不用 ANTLR 的原因是我实在配不好环境—— C++ 真难用（确信）。

在 Parser 产生完整的 AST 之前，常量折叠就开始了。这是为了解析数组的长度：

```cpp
const int x[2] = { 1, 2 };
const int y[x[1]] = ...;
```

考虑到 `x[1]` 是编译期常量，这应当是合法的。为了正确产生 `y` 的类型，必须在这时就开始折叠。

接下来是语义分析，主要是标记类型，并插入 int/float 转换的 AST 节点。

### CodeGen

CodeGen 所生成的 IR 参考了 MLIR 的 `scf` 方言的设计方式。作为一个例子，考虑这样的一段代码：

```cpp
int main() {
  int i = 0, sum = 0, n = getint();
  while (i < n) {
    sum = sum + i;
    i = i + 1;
  }
  putint(sum);
}
```

它会生成这样的 IR：

```mlir
%0 = module {
  %1 = func <name = main> <count = 0> {
    %2 = alloca <size = 4>
    %3 = alloca <size = 4>
    %4 = alloca <size = 4>
    %5 = int <0>
    %6 = store %5 %2 <size = 4>
    %7 = int <0>
    %8 = store %7 %3 <size = 4>
    %9 = call <name = getint>
    %10 = store %9 %4 <size = 4>
    %11 = while {
      %12 = load %2 <size = 4>
      %13 = load %4 <size = 4>
      %14 = lt %12 %13
      %15 = proceed %14
    }{
      %16 = load %3 <size = 4>
      %17 = load %2 <size = 4>
      %18 = addi %16 %17
      %19 = store %18 %3 <size = 4>
      %20 = load %2 <size = 4>
      %21 = int <1>
      %22 = addi %20 %21
      %23 = store %22 %2 <size = 4>
    }
    %24 = load %3 <size = 4>
    %25 = call %24 <name = putint>
    %26 = int <0>
    %27 = return %26
  }
}
```

这里的 IR 依旧保存了结构化控制流。虽然 `module`, `while` 等 Op 也有一个形式上的返回值，但它们实际上不会被使用。

此外，在打印出的 IR 中以 `<>` 包裹的是属性（`Attr*`）。它们不属于操作数。

这样的树形 IR 主要是为了方便代码生成。它会被 FlattenCFG 展平，形成和 LLVM IR 一样的模式。但如果将某些 Pass 放在 FlattenCFG 之前，它们会变得比较简单。

### Pass

完整的 Pass 管线在下面列出。截至 2025/6/4，此处共 81 个 Pass，其中不重复的有 49 个。点击对应的链接可以跳转到本文附录处，查看每个 Pass 的作用。

1. [MoveAlloca](#MoveAlloca)
1. [AtMostOnce](#AtMostOnce)
1. [Localize](#Localize)
1. [EarlyConstFold](#EarlyConstFold)
1. [Pureness](#Pureness)
1. [EarlyConstFold](#EarlyConstFold)
1. [TCO](#TCO)
1. [Remerge](#Remerge)
1. [RaiseToFor](#RaiseToFor)
1. [DCE](#DCE)
1. [EarlyInline](#EarlyInline)
1. [ArrayAccess](#ArrayAccess)
1. [Base](#Base)
1. [View](#View)
1. [LoopDCE](#LoopDCE)
1. [Fusion](#Fusion)
1. [Unswitch](#Unswitch)
1. [Lower](#Lower)
1. [FlattenCFG](#FlattenCFG)
1. [GVN](#GVN)
1. [DCE](#DCE)
1. [Inline](#Inline)
1. [DCE](#DCE)
1. [Localize](#Localize)
1. [Globalize](#Globalize)
1. [Mem2Reg](#Mem2Reg)
1. [Alias](#Alias)
1. [RegularFold](#RegularFold)
1. [DCE](#DCE)
1. [DAE](#DAE)
1. [Alias](#Alias)
1. [DSE](#DSE)
1. [DLE](#DLE)
1. [GVN](#GVN)
1. [CanonicalizeLoop](#CanonicalizeLoop)
1. [LoopRotate](#LoopRotate)
1. [CanonicalizeLoop](#CanonicalizeLoop)
1. [LICM](#LICM)
1. [ConstLoopUnroll](#ConstLoopUnroll)
1. [SCEV](#SCEV)
1. [GVN](#GVN)
1. [RegularFold](#RegularFold)
1. [DCE](#DCE)
1. [GVN](#GVN)
1. [SimplifyCFG](#SimplifyCFG)
1. [Alias](#Alias)
1. [DAE](#DAE)
1. [DSE](#DSE)
1. [DLE](#DLE)
1. [Select](#Select)
1. [RegularFold](#RegularFold)
1. [DCE](#DCE)
1. [GCM](#GCM)
1. [GVN](#GVN)
1. [AggressiveDCE](#AggressiveDCE)
1. [LateInline](#LateInline)
1. [RegularFold](#RegularFold)
1. [GVN](#GVN)
1. [Alias](#Alias)
1. [DSE](#DSE)
1. [DLE](#DLE)
1. [DCE](#DCE)
1. [InlineStore](#InlineStore)
1. [SynthConstArray](#SynthConstArray)
1. [RegularFold](#RegularFold)
1. [DCE](#DCE)
1. [GCM](#GCM)
1. [GVN](#GVN)
1. [CanonicalizeLoop](#CanonicalizeLoop)
1. [SCEV](#SCEV)
1. [RemoveEmptyLoop](#RemoveEmptyLoop)
1. [GVN](#GVN)
1. [RegularFold](#RegularFold)
1. [CanonicalizeLoop](#CanonicalizeLoop)
1. [SCEV](#SCEV)
1. [RemoveEmptyLoop](#RemoveEmptyLoop)
1. [GVN](#GVN)
1. [RegularFold](#RegularFold)
1. [AggressiveDCE](#AggressiveDCE)
1. [SimplifyCFG](#SimplifyCFG)
1. [InstSchedule](#InstSchedule)

### 后端

后端是纸糊的，差不多能跑。

寄存器分配和其他编译器内常见的策略不太一样。在分配的时候，phi 节点尚未被摧毁。下面我将详细描述整个流程。

- 构建冲突图，并记录 phi 和它的操作数，将它们标记为“想要分配在一起”。

- 分配着色优先级。初始时，有一个变量 `int priority = 0`。
  - 遇到 phi 时，令它的优先级为 `priority + 1`, 它的所有操作数的优先级为 `priority`. 然后令 `priority` 永久 +2.
  - 遇到 -2048-2047 内的 li 指令时，令它的优先级为 -2. 这是因为它如果被 spill，不必分配栈上空间，只需要一条指令就可以加载回来。
  - 遇到不在上述范围内的 li 指令时，令它的优先级为 -1. 如果它被 spill，虽然不必分配栈上空间，但需要两条指令才能加载。
  - 遇到 readreg/writereg 的伪指令时，令它的优先级为 1. 这是为了尽量早分配它，从而减少一条 mv 指令。
  - 其他的指令的优先级为 0.

- 开始着色。按照优先级的降序排列，如果相同则按照图中度数的降序排列。
  - 遇到 phi 时，尽量不分配已知会和它操作数冲突的寄存器。

- 移除所有 Op 的操作数，并用 `<rd = ...>` 等属性取代。至此，def-use chain 完全破裂。

- 移除 readreg/writereg 等伪指令，用 mv 代替。

- 拆除 phi。首先割开 critical edge，然后在 phi 的每个前驱补充一条 mv 指令。
  - 需要先进行拓扑排序，保证 phi 拆除的顺序是正确的。
  - 不考虑在同一个基本块内循环引用的 phi （所有的测试用例中都不会出现这样的 phi）。

- 为 spill 出的变量添加 ld/sd 指令。

# 附录

## Pass 简介

此处按照字母顺序排序。

### AggressiveDCE

死代码删除。它初始假定所有代码都不可达，然后进行数据流分析找到可达的代码。

这可以消除两个循环引用彼此的 phi，但普通的 DCE 认为它们的 use 都不为空，所以不会删除它们。

### Alias

别名分析。分析一个地址来自哪个数组，以及它的偏移量是多少。

它可以跨函数分析。

### AtMostOnce

分析某个函数是否只会被调用一次。结果会作为属性 `<once>` 添加到 FuncOp 上。

### CallGraph

构建调用图。结果会作为属性 `<caller = ...>` 添加到 FuncOp 上。

### CanonicalizeLoop

标准化循环。

会保证循环有一个 preheader，同时可以选择是否构建 LCSSA。

### DAE

删除无用的参数。这包括：

- 通过跨函数分析得知必定为常量的参数；
- 从未被用到的参数。

### DCE

死代码删除。

会删除不可达的基本块，未使用的指令和未调用的函数。

### DLE

删除无用的读取。如果我们知道上次写入这个地址的值，就无需再次读取。这只在一个基本块内生效。

### DSE

删除无用的写入。利用数据流分析在整个函数内生效。

### FlattenCFG

将结构化控制流展平。

### GCM

Global Code Motion. 将语句移动到尽可能深的 if 内，尽可能浅的循环外。

### Globalize

在安全的情况下，将局部数组提升至全局。

### GVN

Global Value Numbering。用来消除公共表达式。

### HoistConstArray

（未被使用）仍有 bug。

### Inline

函数内联，在 Mem2Reg 之前进行。内联条件是函数体内的指令数不超过 200 ，而且函数自身不递归。

### InlineStore

如果一个 StoreOp 向某个全局变量的确定位置写入，在安全的情况下，直接将它并入全局变量的初始化列表（`.data` 段）中。

### InstSchedule

指令重排。很玄学，不知道我这个重排究竟是否有优化。

只在基本块内部进行，每次从可选的指令中挑出优先级最高的。优先级如下：

- IntOp, GetGlobalOp 和 FloatOp 的优先级是 -3000.
- 对于这个基本块内的 phi 的某个操作数，它的优先级是 -5000，也就是尽量留到最后。
- 如果这条指令的操作数是一个 load，而且指令本身在 load 的 2 条指令之内，优先级是 -1.
- 如果这条指令的操作数是一个 mul，而且指令本身在 mul 的 8 条指令之内，优先级是 -1.
- 如果此时优先级小于零，那么不再进行下面的调整，直接返回优先级。
- 如果这条指令是一个 load，优先级 +8.
- 如果这条指令离自身最远的、在同一个基本块内的操作数的距离是 x, 那么优先级不会低于 x/3.

### LateInline

内联，和 Inline 效果一样，但在 Mem2Reg 之后进行。它需要特别处理 phi。

### LICM

Loop Invariant Code Motion. 虽然 GCM 可以把大部分东西都移走，但它无法搬动 load/store。LICM 就是专门来搬它们的。

### LoopAnalysis

识别循环结构，构造循环森林，并找出每个循环的归纳变量（如果有的话）。

### LoopRotate

循环旋转。相当于把 while 改为 if + do-while 的形式。

### LoopUnroll

循环展开。只会展开循环次数为常数的循环，而且要求展开之后的总指令数不能超过 1000.

### Mem2Reg

将一些 alloca 和它们的 load/store 转化为 phi。

### Pureness

分析函数是否是纯的。纯的函数应当没有副作用，而且对于相同的输入给出相同的输出。

### Range

（尚未使用）分析整数的取值范围。

由于测试用例依赖整数溢出的未定义行为（它必须表现得像截断），这个 pass 暂时无法使用。

### RangeAwareFold

同上，暂时无法使用。

### RegularFold

根据编译器内部的一个 DSL 进行模式匹配与重写。

举一个例子：
```lisp
(change (sub x x) 0)
```
意味着将 (x - x) 改为 0.

它还支持比这复杂得多的例子：
```lisp
(change (div (mul x 'a) 'b) (!only-if (!eq (!mod 'a 'b) 0) (mul x (!div 'a 'b))))
```
只在 `a % b == 0` 时，将 `(x * a) / b` 改为 `x * (a / b)`.

这个 DSL 只支持根据 def-use 的匹配，无法匹配基本块的走向等内容。

不过 RegularFold 确实会折叠条件已知的 branch。这不是依赖 DSL 的，而是普通的 C++ 代码。

### RemoveEmptyLoop

顾名思义，删除空的循环。

### SCEV

针对循环的归纳变量进行分析。它比 LLVM 中的 SCEV 弱一些，不会尝试折叠类似 mul, div, smin, umin 之类的东西。

以 `for (int i = x; i < y; i += c)` 为例（它要求 `c` 是常量），它能做的事情包含：
- 将循环外面对 `a = a + k` (k 是常数) 的使用替换为 `a = a0 + k * n`，其中 `n` 是循环次数；
- 同理，将以“二阶”速度增加的 `a = a + k * i` 折叠为 `a = a0 + k * n(n + 1) / 2`，其中 `n` 是循环次数。
- 将循环内部的 `a[i]` 替换为 `a = a+4`，如果这个替换使得 `i` 无用，它也可以删除 `i`；
- 将循环内部的 `a = (a + x) % mod` 替换为 `a = a + x` （这里的加法是 64 位的），并在循环完成后再 `% mod`. 这不会溢出，因为循环至多进行 2^32 次，每次加的数也不会超过 2^31.

### Select

(README 尚未完工)
