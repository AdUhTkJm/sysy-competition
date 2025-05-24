# 概述

这个编译器受到了不少 MLIR 的启发。这里的 IR 就是模仿它设计的。

这个编译器吸收了一些我的另一个编译器项目[Moonbit 编译器](https://github.com/AdUhTkJm/moonbit-mlir)的经验。这里的 `parse/TypeContext.h` 管理内存的方式就来自这个项目（不过实际上 interning 也是很常用的技术）。

每个 Op 都有恰好一个返回值，不定数量的操作数（`Value`，对 `Op*` 的一层包装），一些子作用域 (`Region*`，实际上是基本块的容器)，以及一些属性 (`Attr*`)。Op 本身并不对任何东西进行检查，但 Pass 会假定某些 Op 具有特定的操作个数和属性（例如 `AddIOp` 有恰好两个操作数）。

这样会让 Lowering 略微方便一些：比起将它转译为某种类似 MCInst 的东西，我可以直接利用已有的 Op 设施。不过我并不确定这是否是个好主意——或许使用 MCInst 反而会方便一些？

# 编译器结构

## Parser

Lexer 和 Parser 是手写的，其中 Parser 是简单的递归下降。不用 ANTLR 的原因是我实在配不好环境—— C++ 真难用（确信）。

在 Parser 产生完整的 AST 之前，常量折叠就开始了。这是为了解析数组的长度：

```cpp
const int x[2] = { 1, 2 };
const int y[x[1]] = ...;
```

考虑到 `x[1]` 是编译期常量，这应当是合法的。为了正确产生 `y` 的类型，常量折叠是必要的。

接下来是语义分析，主要是标记类型，并插入 int/float 转换的 AST 节点。

## CodeGen

CodeGen 所生成的 IR 参考了 MLIR 的 `scf` 方言的设计方式。作为一个例子，考虑这样的一段代码：

```cpp
int main() {
  int a = 7;
  int count = 0;
  while (a != 1) {
    count = count + 1;
    if (a % 2 == 0)
      a = a / 2;
    else
      a = a * 3 + 1;
  }
  return count;
}
```

它会生成这样的 IR：

```mlir
%0 = module {
  %1 = func <name = main> {
    %2 = alloca <4>
    %3 = int <7>
    %4 = store %3 %2 <size = 4>
    %5 = alloca <4>
    %6 = int <0>
    %7 = store %6 %5 <size = 4>
    %8 = while {
      %9 = load %2 <size = 4>
      %10 = int <1>
      %11 = ne %9 %10
      %12 = proceed %11
    }{
      %13 = load %5 <size = 4>
      %14 = int <1>
      %15 = addi %13 %14
      %16 = store %15 %5
      %17 = load %2 <size = 4>
      %18 = int <2>
      %19 = modi %17 %18
      %20 = int <0>
      %21 = eq %19 %20
      %22 = if %21 {
        %23 = load %2 <size = 4>
        %24 = int <2>
        %25 = divi %23 %24
        %26 = store %25 %2
      }{
        %27 = load %2 <size = 4>
        %28 = int <3>
        %29 = muli %27 %28
        %30 = int <1>
        %31 = addi %29 %30
        %32 = store %31 %2
      }
    }
    %33 = load %5 <size = 4>
    %34 = return %33
  }
}
```

值得注意的是 IR 依旧保留了某种树形结构。其中类似 `module`, `func` 和 `if` 的 Op 形式上会返回一个值，但实际上没有任何地方会用到它们——写 `std::optional<Value>` 还是有点麻烦，反正这样做也无害，所以就这样了。

此外，在打印出的 IR 中以 `<>` 包裹的是属性（`Attr*`）。它们不属于操作数。

这样的树形 IR 主要是为了方便代码生成。它会被 FlattenCFG 展平，形成和 LLVM IR 一样的模式。但如果将某些 Pass 放在 FlattenCFG 之前，它们会变得比较简单。

## Pass

### 整理

CodeGen 生成的代码依旧有不正确之处。在运行其他 Pass 之前，需要先运行这些 Pass 来纠正它。

**MoveAlloca**

将函数里的所有 `alloca` 移到函数的最前方：不管原来这个 `alloca` 是处在 if 还是 while 中，它本来都应该只被执行一次。

**FlattenCFG**

展平控制流。将 IfOp 和 WhileOp 展开，变为 Goto, Branch 和基本块。这类似于 MLIR 的 `scf` 到 `cf` 的转换。

它的前后都有不少 Pass。有些 Pass 喜欢结构化的控制流，有些喜欢基本块。

在这个 Pass 结束后，除了 `module` 和 `func` 外的 Region 彻底消失，每个基本块都相互独立，可以随意移动了。

### 分析

**CallGraph**

分析函数调用链。简单直接。

**Pureness**

分析函数是否是纯函数。给所有不纯（有副作用，或对同样的输入可能产生不同输出）的 FuncOp 打上 `<impure>`。

在 SysY 中，由于不存在二级指针，一个函数不纯意味着以下三种情况之一：
- 它访问了全局变量（无论读/写都算）；
- 它访问了指针类型的参数所指向的值；
- 它调用了另一个不纯的函数。

检查是否有 GetGlobalOp 即可检查第一点。在 IR 生成的时候，就给所有含有指针参数的函数都打上了 `<impure>`，因此无需检查第二点。

至于第三点，这个 pass 会在调用图上传播是否有副作用的信息（当然，所有库函数都是不纯的）。

**AtMostOnce**

分析一个函数是否至多会被调用 1 次。如果是，就给它打上 `<once>`。

它在 FlattenCFG 之前运作，因为这样“至多调用一次”的条件会变得非常简单。对于一个函数，只需要检测：
- 仅被至多一个函数调用；
- 在那个函数里的 CallOp 中，只有一个是指向它的；
- 那个 CallOp 不在 WhileOp 里。

**Alias**

别名分析。这是控制流不敏感的，但会在函数之间传播信息。这也意味着 AliasAttr 可以指向自身所在函数之外的 Op。

SysY 不存在二级指针，这使得别名分析十分便利。

我不准备处理 Phi；如果 Phi 的操作数都是指针，那么它只可能由 SCEV 产生，这说明它原本就在循环里，无法分析更具体的指向。

**Range**

TODO：分析整数范围。也许可以弄一个不需要 e-SSA 的形式，只考虑正负？

**LoopAnalysis**

通过检测回边，识别函数中的循环结构。这也算是经典算法了。

### 优化

#### 清理型 (CleanupPasses.h)

**RegularFold**

利用 Lisp-like DSL 进行匹配与改写。这个 DSL 在编译器的其他地方也有运用，例如识别循环变量时，以及 ARM 后端中。

举个比较复杂的例子：
```lisp
(change (eq (div x 'a) 'b) (!only-if (!gt 'a 0) (and (lt x (!mul (!add 'b 1) 'a)) (ge x (!mul 'b 'a)))))
```

这意味着匹配形如 `x / A = B` （其中 A, B 均为常数）的指令，并只在 A > 0 的时候将它改写为 `(x < (B + 1) * A) && (x >= B * A)`。

这里的改写只会改写最外层的指令；例如在上面的那条规则中，x, A, B 都不会被改写。

此外，条件固定为 0 或固定为 1 的分支也会被改写为 GotoOp。

基于这个 Pass 衍生出了 EarlyConstFold，在 FlattenCFG 之前进行，会做更多不便用这种 DSL 描述的折叠（例如与数组有关的折叠）。

**DCE**

死代码删除。首先决定每个指令是否需要 `<impure>`，其中 CallOp 的结果根据 Pureness 打上的标记决定。

接下来，所有 `getUses()` 为空的，而且没有被标记为 `<impure>` 的 Op 都会被删除。这包括一些具有 Region 的 Op，例如 `IfOp`。

同时，这个 pass 还会分析不可达的函数和基本块，并删除它们。

在 FlattenCFG 之前，基本块的末尾不一定有跳转指令，需要特别注意不能删除不可达的基本块。

这不会删除循环引用的指令。它们要靠 AggressiveDCE 进行数据流分析后删除。

**Inline**

函数内联。放在 FlattenCFG 之后是因为 early return 基本上没法用 if 和 while 来表示，会破坏 Region* 的结构。

目前的内联条件：
- 不是递归函数 （TODO：对递归函数单独内联）；
- 指令数 <= 200。

这个 Pass 在 Mem2Reg 之前运行。在它之后还有 LateInline，会调整 Phi 的参数。

**Mem2Reg**

将 AllocaOp 转化为普通的 SSA 值。从未被赋值的 alloca 会被赋予 0 的初值（否则编译器会直接段错误）。

**GVN**

通过给 SSA 值标号进行公共子表达式删除。它能够删除的表达式是白名单的。

值得注意的是，没有标记为 `<impure>` 的 CallOp 也是可以被合并的。

这个 Pass 会把所有常量合在一块，会导致这样的代码（84_long_array.sy）产生巨大的寄存器压力：
```c
int b[4][1024] = { 1 };
int c[1024][4] = { 2 };
```
因为在代码生成的时候，它实际上生成的是 `*(b + 0) = 1, *(b + 4) = 0` 等一共4096条指令。这4096个常数和 `*(c + 0) = 2` 中的常数完全一致，导致在为 `c` 初始化时有4000多个溢出的寄存器。

目前的缓解方法是运行 Globalize 这个 pass。虽然 rematerialization 可以解决这个问题，但它尚未提上日程。这主要是因为整个 spill 的处理都十分混乱。

**Localize**

将全局变量变为局部变量。

如果一个全局变量不是数组，而且只在某个 `<once>` 的函数（见 AtMostOnce）中被访问，那么它可以被改写为局部变量。

**Globalize**

将局部变量变为全局变量。

如果一个局部变量是至少有 8 个元素的数组（对于太小的数组，或许还是放在栈上比较好），而且定义它的函数是 `<once>` 的，那么它可以被改写为全局变量。

这之后，顺次检查这个函数中所有的 StoreOp 和 LoadOp，直到遇到写入编译器不确定的地址或者 BranchOp 为止。如果这些 Store 是编译期常量，那么直接将它写入数组的初始数据中，否则记录下来，等待编译器能确定的 LoadOp 的读取。

这可以部分解决上述 GVN 提到的问题。

**DSE**

Dead Store Elimination。通过类似活跃变量分析的数据流方法，识别不会被读取的 StoreOp。

它十分保守，只消除满足下列条件的 StoreOp：
- Alias 不是 `<unknown>`；
- 不可能指向任何一个全局变量；
- 不可能指向任何一个不在自身函数内的变量（例如，函数的参数就可以指向函数外的变量）。

**DLE**

Dead (Redundant) Load Elimination。与 DSE 类似，但只局限于一个基本块之内。

为了比较好的消除效果，这需要 SimplifyCFG 的辅助。

**SimplifyCFG**

将只有一个后继，而且那个后继也只有一个前驱的基本块跟它的后继合并。

听起来很绕，但总之就是把“一条直线”上的基本块合成一个。

**CanonicalizeLoop**

将循环标准化。它本身并不做任何的优化，但是会为其它的优化 Pass 铺路。

它首先会给循环生成 preheader。值得注意的是，这会导致 header 的 phi 几乎全部失效（不再指向前驱块），需要特别处理。后面的 Verify 会报错，但排查问题到底出现在哪个 Pass 还是非常繁琐的一件事情。

接下来它生成 LCSSA 形式，为每个在循环之外使用的变量生成 PhiOp。这些额外的 phi 如果只有一个操作数，就会在 GVN 中消失。

**LoopRotate**

旋转循环。

首先，确保每个循环只有一个 latch。将位于 header 的分支复制一份提前到 preheader，然后把原本那份移动到 latch。

这本身也不算什么优化，但这之后的 RegularFold 可能可以消除一次判断。这也方便了接下来的循环展开。

**LoopUnroll**

循环展开，但只展开执行常数次的循环。

TODO：更多的展开可能会在 FlattenCFG 前进行。

**GCM**

Global Code Motion。它会留下 load/store 和跳转指令不移动，然后将所有的指令按照如下规则移动两次：

首先，将所有的指令尽可能提前，也就是首个被所有的操作数支配的块；然后，将所有的指令尽可能延后，也就是最后一个支配所有使用者的块。

接下来，在支配树上，这两个块之间的路径上的所有块都是可选的。我们会选择尽可能靠后，但嵌套在最浅的循环中的块。

## RISC-V 后端

值得注意的是，后端依然与 IR 共用一套 Op 系统。这说明了 Op 设计的灵活性。

**Lower**

类似 LLVM 中的 legalization。将 IR 转化为汇编指令，并进行指令选择。

**InstCombine**

匹配并改写指令。虽然说是 combine，但其实也可以展开（虽然现在还没有展开）。

被合并的指令包括：

- li + add => addi （对 sub, and 同理）
- addi + sw => sw （改变offset）
- addi + lw => lw （改变offset）
- 去除 `%2 = addi %1, 0`
- `li %1, 0` => `%1 = rv.readreg <reg = zero>`

**RegAlloc**

寄存器分配。分配完成后，所有 Op 的操作数都被移除，取而代之的是一系列属性。例如对于操作 `%1 = rv.add %2 %3`, 在这个 pass 执行完毕后可能会转变为：

```
rv.add <rd = a0> <rs = a1> <rs2 = a2>
```

这也就意味着 use-def chain 破裂，pass 到此为止。

同时，RegAlloc 还会将 PhiOp 降低到 `mv` 指令，并整理生成的汇编。

这个 pass 是整个编译器里最复杂的一个 pass，它的具体执行方式如下：

1. 在 CallOp 和 GetArgOp 周围插入已经填色的占位指令，用来标记它们所污染的寄存器。
  - 对于 `getarg <8>` 及获取更后面参数的 GetArgOp，它们和 a0 - a7 都冲突。
  - 对于 `getarg <7>` 及之前的 GetArgOp，它们直接在这里被改写为 ReadRegOp。

2. 完成活跃变量分析，构建冲突图。
  - 对于 WriteRegOp 和 ReadRegOp，它们偏好的寄存器会被记录下来，并获得 1 的优先级。
  - 对于 PhiOp，它会获得 2 的优先级，而它的参数会获得 1 的优先级，并且会尽可能与 Phi 本身分配同一个寄存器。
  - 其它的所有 Op 都是 0 优先级。

3. 根据 (优先级, 冲突图中的度数) 的字典序排序，并分配寄存器。分配完成后，额外分配栈空间以满足 spilling 的需要。
  - 两个寄存器 s11 和 t6 没有被分配，用来临时加载 spill 的变量。

4. 将所有未被 spill 的 SSA 变量转化为 RdAttr 之类的属性。去除 1. 中引入的占位指令。

5. 摧毁 PhiOp。
  - 对于所有至少有两个后继的基本块，将 CFG 中对应的边切割开来。
  - 将 Phi 转化为它前驱中的 mv 指令。
  - 检测是否有循环赋值 (swap problem)。如果有，就利用 s11 正确完成交换。

6. 将所有被 spill 的 SSA 变量转化为 `ld s11, OFFSET(sp)`（或者 t6 ，如果一条指令用了两个被 spill 的变量的话）。
  - 对于 `OFFSET >= 2048` 的情况，这实际上是三条指令。（TODO：2048-4096之间可以变成两条）
  - 在给需要超过8个参数的函数传递参数的时候，需要 `addi sp, sp, -<OFFSET2>`。这意味着之前算出来的 spill offset 也需要加上这个 OFFSET2。

7. 记录每个函数使用了哪些寄存器。

8. 产生函数的 prologue/epilogue。
  - 再给 stack frame 扩大一点。在这里将大小变为 16 的倍数，而在前面扩大的时候并没有管对齐。

9. 窥孔优化。
  - 若有对同一个地址的 sw + lw，可以将 lw 改为 mv。
  - 若有两个连续的 sw zero，可以改为 sd。这要求地址是 8 字节对齐的，所以只折叠了 sp（毕竟 sp 保证是 16 字节对齐的）。
  - 去除 mv a0, a0。

10. 简化控制流。
  - 对于只有一条指令的基本块，如果那条指令是 j，就可以完全消除它。注意这可能会跳到另一个仅含有 j 的基本块，需要计算“跳转闭包”。
  - 直到这里为止，所有的 b 系列（条件跳转）指令都还有一个 TargetAttr 和一个 ElseAttr。根据基本块现在的位置决定该如何消除其中一个 Attr。
  - 删除跳到下一个基本块的 j 指令。

**Dump**

将 RegAlloc 生成的汇编输出。
