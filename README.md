# 概述

这个编译器受到了不少 MLIR 的启发。这里的 IR 就是模仿它设计的。

这个编译器吸收了一些我的另一个编译器项目[Moonbit 编译器](https://github.com/AdUhTkJm/moonbit-mlir)的经验。这里的 `parse/TypeContext.h` 管理内存的方式就来自这个项目（不过实际上 interning 也是很常用的技术）。

每个 Op 都有恰好一个返回值，不定数量的操作数（`Value`，对 `Op*` 的一层包装），一些子作用域 (`Region*`，实际上是基本块的容器)，以及一些属性 (`Attr*`)。Op 本身并不对任何东西进行检查，但 Pass 会假定某些 Op 具有特定的操作个数和属性（例如 `AddIOp` 有恰好两个操作数）。为了简化工作，值都不具有类型，而类型有关的信息会在 CodeGen 阶段作为属性生成。

这样灵活的 Op 使得编写 Lowering 的工作更加简单。比起将它转译为某种类似 MCInst 的东西，我可以直接在已有的 Op 上改写。同时，这也方便了 Pass 的编写，使得在 Pass 运行过程中没有任何额外的不变量需要保持。这是和 LLVM 较大的不同之处。

Op 本身还追踪所有的使用者。这使得极为强大的 `replaceAllUsesWith` 得以实现。

这个编译器大量运用了 CRTP 和手工的 RTTI。尽管这里没有 -fno-rtti，我也习惯了使用 dyn_cast, cast 和 isa，而不是自带的 dynamic_cast。所以我在 `utils/DynamicCast.h` 里手动搓了一个。

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

## Pass

### 整理 CodeGen

CodeGen 生成的代码依旧有不正确之处。在运行其他 Pass 之前，需要先运行这些 Pass 来纠正它。

**MoveAlloca**

将函数里的所有 `alloca` 移到函数的最前方：不管原来这个 `alloca` 是处在 if 还是 while 中，它本来都应该只被执行一次。

### 分析 Pass

**Pureness**

分析函数是否是纯函数。给所有不纯（有副作用，或不 idempotent）的 FuncOp 打上 `ImpureAttr`。

在 SysY 中，由于不存在二级指针，一个函数不纯意味着以下三种情况之一：
- 它修改了全局变量（无论读/写都算）；
- 它修改了指针类型的参数所指向的值；
- 它调用了另一个不纯的函数。

遍历所有的 GlobalOp 即可检查第一点，而通过对 GetArgOp 的分析可以检查第二点。

至于第三点，这个 pass 会在 call graph 上传播是否有副作用的信息。它也会据此决定 CallOp 纯不纯。

### 优化 Pass

**DCE**

死代码删除。首先决定每个指令是否需要 `<impure>`，其中 CallOp 的结果根据 Pureness 打上的标记决定。

接下来，所有 `getUses()` 为空的，而且没有被标记为 `<impure>` 的 Op 都会被删除。这包括一些具有 Region 的 Op，例如 `IfOp`。

这个 pass 会被反复调用，不论是在 CodeGen 刚结束的时候，还是 FlattenCFG 后，还是后端中。

**StrengthReduct**

窥孔优化。主要优化单个指令。

这里只识别较简单的 pattern，并不会如同 2024 年 BUAA 队伍们一样识别“恰好含有11个指令的基本块”。

进行的优化包括：

-  对于 `... * x`：
  - 如果 `x` 是 2 的次幂，变为左移；
  - 如果 `x` 有两个 bit 是 1，变为两个左移的加法；
  - 如果 `x` 加上某个 2 的次幂会变成 2 的次幂，变为两个左移的减法。

- 对于 `... / x`：
  -   

**Mem2Reg**

将 AllocaOp 转化为普通的 SSA 值。

### Lowering

将 IR 转化为更加贴近汇编的形态。每个后端都有自己的 lowering passes，这里给出的是公用部分。

**FlattenCFG**

展平控制流。将 IfOp 和 WhileOp 展开，变为 Goto, Branch 和基本块。这类似于 MLIR 的 `scf` 到 `cf` 的转换。

在这个 Pass 结束后，除了 `module` 和 `func` 外的 Region 彻底消失，每个基本块都相互独立，可以随意移动了。

## RISC-V 后端

值得注意的是，后端依然与 IR 共用一套 Op 系统。这说明了 Op 设计的灵活性。

**Lower**

类似 LLVM 中的 legalization。将 IR 转化为汇编指令，并进行指令选择。

**InstCombine**

匹配并改写指令。虽然说是 combine，但其实也可以展开（虽然现在还没有展开）。

被合并的指令包括：

- li + add => addi

**RegAlloc**

寄存器分配。分配完成后，所有 Op 的操作数都被移除，取而代之的是一系列属性。例如对于操作 `%1 = rv.add %2 %3`, 在这个 pass 执行完毕后可能会转变为：

```
rv.add <rd = a0> <rs = a1> <rs2 = a2>
```

这也就意味着 use-def chain 破裂，pass 到此为止。

同时，RegAlloc 还会将 PhiOp 降低到 `mv` 指令，并整理生成的汇编。

**Dump**

将 RegAlloc 生成的汇编输出。
