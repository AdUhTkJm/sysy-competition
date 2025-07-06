# 概述

这个编译器受到了不少 MLIR 的启发。这里的 IR 就是模仿它设计的。

每个 Op 都有恰好一个返回值，不定数量的操作数（`Value`，对 `Op*` 的一层包装），一些子作用域 (`Region*`，实际上是基本块的容器)，以及一些属性 (`Attr*`)。Op 本身并不对任何东西进行检查，但 Pass 会假定某些 Op 具有特定的操作个数和属性（例如 `AddIOp` 有恰好两个操作数）。

这样会让 Lowering 略微方便一些：比起将它转译为某种类似 MCInst 的东西，我可以直接利用已有的 Op 设施。这就相当于 MLIR 的 dialect conversion。不过，这会导致一些后端特有的性质难以表示（见下），但当我发现这一点时已经不好修改架构了。

# 编译器结构

## 子工程

### SMT 求解器

在 `utils/smt` 中，我手工制作了一个简单的 SMT 求解器，可以求解不含量词的 bitvector 理论（QF_BV）。换言之，它可以判定两个涉及 32 位整数的算式是否相等，在相等时给出证明，不等时给出反例。

首先，我实现了一个基于 CDCL 算法的 SAT 求解器。接下来，对于 32 位整数，只需要将其拆成 32 个独立的布尔变量，按照数字电路的方式组合，就可以实现基本的运算。这意味着这个 SMT 求解器几乎没有做任何优化，执行效率较低。实际上，这也意味着它几乎用不了：一旦带有除法或取余，它会产生数十万个子句，而我的 SAT 求解器最多只能处理 3 万个子句左右的内容。这会导致类似 Souper 编译器的超优化无法进行。因此，不建议之后的队伍们自行实现 SMT 求解器：它的性价比并不高。

## 本体

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

这里的 IR 依旧保存了结构化控制流。虽然 `module`, `while` 等 Op 也有一个形式上的返回值，但它们实际上不会被使用。由于每个 Op 仅能返回一个值，我无法实现类似 `scf.yield` 的内容，不过目前来说，这种设计已经够用了。

在打印出的 IR 中以 `<>` 包裹的是属性（`Attr*`）。它们不属于操作数。

这样的树形 IR 能够方便代码生成，同时能让循环优化变得直观。在 `pre-opt` 文件夹中的一系列优化结束后，它会被 FlattenCFG 展平，形成和 LLVM IR 类似的模式，然后被 `opt` 中的 pass 进一步优化。

### Pass

截至 2025/7/2，中端共 55 个不重复的 pass。而在管线中，有些 pass 会被重复注册，同时有些 pass 会在不注册的情况下隐式地调用其他 pass。包括两个后端在内，被显式注册的 pass 共有 102 个，实际执行的 pass 数量估计在 130-140 左右。

这两个数值可以由下面的命令得出：

```bash
cd src
find pre-opt opt -name '*.cpp' | wc -l | awk 'END { print $1 - 3 }'
cat main/main.cpp | grep "pm.addPass" | wc -l
```

减去 3 的原因是，我们需要排除 `pre-opt/PreAttrs.cpp`，以及 `opt/Pass.cpp` 和 `opt/PassManager.cpp`。其余的每个 cpp 文件都恰好实现了一个 pass。

具体的 pass 注册顺序可以参见 `main/main.cpp`，而每个 pass 的具体作用和实现原理将会在附录写出。

### 后端

ARM 和 RV 后端有一定共同之处。它们首先将中端 IR 一对一地转化为对应的指令集，然后在 InstCombine 中合并指令。这等效于指令选择。

ARM 中，寄存器的长度信息实际上被指令名称所确定。例如，`add x0, x0, x1` 和 `add w0, w0, w1` 会被后端认为是两个指令：`AddXOp` 和 `AddWOp`。同理，对于“灵活的 operand 2”，每种 operand 2 都对应一条新的指令：`add x0, x0, x1, lsl 2` 和 `add x0, x0, 1` 分别是 `AddXLOp` 和 `AddXIOp`。具体的指令列表可以参见 `arm/ArmOps.h`。

由于 Op 结构的限制，类似 `ldr x1, [x2], #4` 的指令难以表示，因为它含有两个返回值：一个是新的地址，另一个是读取的值。我使用了一种 ad-hoc 的解法：让这个指令的产生紧邻在寄存器分配之前，这时只要保证 `x2` 的冲突图构建正确就可以了。这由 ARM 独有的 PostIncr pass 完成。（RV 没有这个指令。）

两个后端的寄存器分配是相似的，而且和其他编译器内常见的策略不太一样。在分配的时候，phi 节点尚未被摧毁。下面我将详细描述整个流程。对于形如 `a / b` 的描述，我们默认 a 指的是 RISC-V 后端，而 b 指的是 ARM 后端。

- 构建冲突图，并记录 phi 和它的操作数，将它们标记为“想要分配在一起”。

- 分配着色优先级。初始时，有一个变量 `int priority = 0`。
  - 遇到 phi 时，令它的优先级为 `priority + 1`, 它的所有操作数的优先级为 `priority`. 然后令 `priority` 永久 +2.
  - 遇到 -2048-2047 内的 li / -16384-16383 范围内的 mov 时，令它的优先级为 -2. 这是因为它如果被 spill，不必分配栈上空间，只需要一条指令就可以加载回来。
  - 遇到不在上述范围内的 li / mov 指令时，令它的优先级为 -1. 如果它被 spill，虽然不必分配栈上空间，但需要两条指令才能加载。
  - 对于 la / adr，同样令它的优先级为 -1. 这也是因为它需要两条指令加载。
  - 遇到 readreg 与 writereg 的伪指令时，令它的优先级为 1. 这是为了尽量早分配它，从而减少一条 mv 指令。
  - 其他的指令的优先级为 0.

- 开始着色。按照优先级的降序排列，如果相同则按照图中度数的降序排列。
  - 遇到 phi 时，尽量不分配已知会和它操作数冲突的寄存器。
  - 留出 2 / 3 个 caller-saved 寄存器，用于 spilling。

- 如果被 spill 的寄存器不超过 2 / 3 个，把 spilling 寄存器当做普通寄存器正常分配。这使得等效可用的寄存器没有减少。

- 移除所有 Op 的操作数，并用 `<rd = ...>` 等属性取代。至此，def-use chain 完全破裂。

- 移除 readreg 与 writereg 等伪指令，用 mv 代替。

- 拆除 phi。首先割开 critical edge，然后在 phi 的每个前驱补充一条 mv 指令。
  - 需要先进行拓扑排序，保证 phi 拆除的顺序是正确的。
  - 如果拓扑排序发现了环，利用一个 spill 寄存器将它拆开。

- 为 spill 出的变量添加 ld 与 sd / ldr 与 str 指令。
  - 如果是上述不需要加载的指令，直接将值存到对应的 spill 寄存器内。

最终，在名为 Dump 的 pass 中输出汇编。如果中端成功并行化了程序的某些部分，那么也会输出 `rt/` 中的手写汇编，用于 Linux syscall，以启动线程、维持同步。

# 附录

此处按照字母顺序排序。

## 结构化控制流上的 Pass

### ArrayAccess

分析数组的访问模式，并将循环内的下标作为 `<subscript>` 属性标记在地址上。

### Base

分析每个地址的 base（它们只能相对 base 偏移）。

不是每个地址都有 base，例如函数参数就没有。

### ColumnMajor

如果判断值得，而且操作安全，那么尝试重排二维数组，将它改为 column-major 的。

### EarlyConstFold

顾名思义，const fold。会调用 [RegularFold](#regularfold)，然后进行一些简单的 load/store 折叠。

### EarlyInline

在结构化控制流中内联。对函数的 return 形式有很高要求，否则在内联后很难保持仍然是 scf 的。

### Fusion

循环融合。只融合边界完全一致的循环。

### Localize

将全局的，只被一个 `<once>` 函数引用的变量变为栈上的。

### LoopDCE

删除无用的循环。

### Lower

将 ForOp（见 [RaiseToFor](#raisetofor)）全部展开为 WhileOp，以方便 FlattenCFG 的运作。

### MoveAlloca

将所有的 AllocaOp 移动到函数开头，以维持后续 pass 期待的性质。

### Parallelizable

判断一个 for 循环是否有 loop-carried dependencies。如果没有，打上 `<parallelizable>` 的属性。

### Parallelize

自动并行化。目前局限性很大，只能并行 embarassingly parallel 的循环。

### RaiseToFor

将符合一定格式的 WhileOp 提升至 ForOp。

### TCO

Tail call optimization。优化尾递归的函数，同时进行 TCO modulo addition。

### TidyMemory

类似 [DSE](#dse)+[DLE](#dle)，整理内存访问。

### Unroll

将所有 for 循环展开 2 次。效果不好，所以未被使用。

### Unswitch

将循环内的分支指令提出。它主要处理和循环变量有关的分支指令，从而将整个 for 循环切分为数个小循环。

### View

尝试识别一个数组是否是另一个的 view (`memref.reinterpret_cast`)。如果是，就将这个 reinterpret_cast 消除。

## 展平 IR 上的 Pass

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

在 [Parallelize](#Parallelize) 处生成的 CloneOp 也算作调用。

### CanonicalizeLoop

标准化循环。

会为循环生成一个 preheader，同时可以选择是否构建 LCSSA。

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

### Inline

函数内联，在 Mem2Reg 之前进行。内联条件是函数体内的指令数不超过 200 ，而且函数自身不递归。

### InlineStore

如果一个 StoreOp 向某个全局变量的确定位置写入，在安全的情况下，直接将它并入全局变量的初始化列表（会输出为汇编的 `.data` 段）中。

### InstSchedule

指令重排。很玄学，不知道我这个重排究竟是否有优化。

注意，这不是针对每个后端进行的，而是在中端进行的。这是因为在 Lower 之后，后端会要求有些指令必须相邻，而 InstSchedule 无法处理这个约束。

只在基本块内部进行，每次从可选的指令中挑出优先级最高的。优先级如下：

- IntOp, GetGlobalOp 和 FloatOp 的优先级是 -3000.
- 对于这个基本块内的 phi 的某个操作数，它的优先级是 -5000，也就是尽量留到最后。
- 如果这条指令的操作数是一个 load，而且指令本身在 load 的 2 条指令之内，优先级是 -1.
- 如果这条指令的操作数是一个 mul，而且指令本身在 mul 的 8 条指令之内，优先级是 -1.
- 如果此时优先级小于零，那么不再进行下面的调整，直接返回优先级。
- 如果这条指令是一个 load，优先级 +8.
- 如果这条指令离自身最远的、在同一个基本块内的操作数的距离是 x, 那么优先级不会低于 x/3.

### LateInline

内联。和 Inline 效果一样，但在 Mem2Reg 之后进行。它需要特别处理 phi。

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

分析整数的取值范围。

会将 branch 指令后的 Op 分裂，从而获取更高精度的结果。

可以跨函数分析，但此时的行为不够精准。

### RangeAwareFold

将正数范围内的除法和取余改为右移和与。

注意，这样做对可能是负数的值是不正确的。对于它们，后端的 StrengthReduct 会专门处理。

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
- 将循环内部的 `a[i]` 替换为 `a = a + 4`，如果这个替换使得 `i` 无用，它也可以删除 `i`；
- 将循环内部的 `a = (a + x) % mod` 替换为 `a = a + x` （这里的加法是 64 位的），并在循环完成后再 `% mod`. 这不会溢出，因为循环至多进行 2^32 次，每次加的数也不会超过 2^31.

### Select

将符合特定模式的基本块改为 select 指令。主要检查这样的子图：

```
bb0: branch %0 <bb1> <else = bb2>
bb1: goto <bb3>
bb2: goto <bb3>
bb3: phi %1 %2 <bb2> <bb3>
```

这时，bb1 和 bb2 什么都没有做，只是为了给 bb3 创造“选择”而存在。

### SimplifyCFG

简化 CFG。将连续的 goto 串起的块合成一个。

### Specialize

（尚未使用）特殊化。

根据参数的正负，创造几个相同的函数，并利用 [RangeAwareFold](#rangeawarefold) 单独优化每个函数。

效果不是很好，所以没有使用。

### SynthConstArray

如果一个常量数组的初始化列表已经给定，尝试用 [SMT 求解器](#smt-求解器)求出一个通项公式，并将对这个数组的访问替换为这个公式。

这要求通项公式的开销不能大于一次 load 的开销，因此选择有限。可以使用某次 commit 中的 `expr-gen.ts` 生成可能的 300 多种选择。

### Vectorize

自动向量化，只在 ARM 中生效（主办方尚未开通 rvv 系列指令）。

如果循环中参与运算的数组的 stride 全部为 1，那么尝试向量化。如果失败，回退到什么也没发生的状态。

### Verify

仅在调试时使用，检查 SSA 的特性是否正确维持。

要求所有 phi 节点的操作数来源于它的前驱，数量正确，而且所有 def 都支配它的 use。
