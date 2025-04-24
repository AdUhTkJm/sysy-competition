# SysY 编译器

## 概述

这个编译器受到了不少 MLIR 的启发。这里的 IR 就是模仿它设计的。

每个 Op 都有恰好一个返回值，不定数量的操作数（`Value`，对 `Op*` 的一层包装），一些子作用域 (`Region*`，实际上是基本块的容器)，以及一些属性 (`Attr*`)。Op 本身并不对任何东西进行检查，但 Pass 会假定某些 Op 具有特定的操作个数和属性（例如 `AddIOp` 有恰好两个操作数）。为了简化工作，值都不具有类型，而类型有关的信息会在 CodeGen 阶段作为属性生成。

这样灵活的 Op 使得编写 Lowering 的工作更加简单。比起将它转译为某种类似 MCInst 的东西，我可以直接在已有的 Op 上改写。同时，这也方便了 Pass 的编写，使得在 Pass 运行过程中没有任何额外的不变量需要保持。这是和 LLVM 较大的不同之处。

Op 本身还追踪所有的使用者。这使得极为强大的 `replaceAllUsesWith` 得以实现。

这个编译器大量运用了 CRTP 和手工的 RTTI。尽管这里没有 -fno-rtti，我也习惯了使用 dyn_cast, cast 和 isa，而不是自带的 dynamic_cast。所以我在 `utils/DynamicCast.h` 里手动搓了一个。

## 编译器结构

### Parser

Lexer 和 Parser 是手写的，其中 Parser 是简单的递归下降。不用 ANTLR 的原因是我实在配不好环境—— C++ 真难用（确信）。

在 Parser 产生完整的 AST 之前，常量折叠就开始了。这是为了解析数组的长度：

```cpp
const int x[2] = { 1, 2 };
const int y[x[1]] = ...;
```

考虑到 `x[1]` 是编译期常量，这应当是合法的。为了正确产生 `y` 的类型，常量折叠是必要的。

接下来是语义分析，主要是标记类型，并插入 int/float 转换的 AST 节点。

### CodeGen

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

### Passes

这个编译器中的 Pass 可以分为四类：调整 CodeGen 生成的不正确代码的 (S)，用于将 IR 降低为更底层的 Op 的 (L)，分析的 (A)，以及优化的 (O)。这里的列举顺序不完全是执行顺序。

**MoveAlloca** - S

将函数里的所有 `alloca` 移到函数的最前方：不管原来这个 `alloca` 是处在 if 还是 while 中，它本来都应该只被执行一次。

**Pureness** - A

给所有不纯（有副作用）的 Op 打上 `ImpureAttr`。这个分析较为细致，并不会认为所有的 CallOp 都不纯；它会在 call graph 上传播是否有副作用的信息，以分析函数是否是纯的。

**FlattenCFG** - L

展平控制流。将 IfOp 和 WhileOp 展开，变为 Goto, Branch 和基本块。这类似于 MLIR 的 `scf` 到 `cf` 的转换。

在这个 Pass 结束后，除了 `module` 和 `func` 外的 Region 彻底消失，每个基本块都相互独立，可以随意移动了。

**Mem2Reg** - O
