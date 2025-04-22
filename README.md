# SysY 编译器

### 概述

这个编译器受到了不少 MLIR 的启发。这里的 IR 就是模仿它设计的。

每个 Op 都有恰好一个返回值，不定数量的操作数，一些子作用域 (`Region*`)，以及一些属性 (`Attr*`)。Op 本身并不对任何东西进行检查，但 Pass 会假定某些 Op 具有特定的操作个数和属性（例如 `AddIOp` 有恰好两个操作数）。

这个编译器大量运用了 CRTP 和手工的 RTTI。尽管这里没有 -fno-rtti，我也习惯了使用 dyn_cast, cast 和 isa，而不是自带的 dynamic_cast。所以我在 `utils/DynamicCast.h` 里手动搓了一个。

### 前端

Lexer 和 Parser 是手写的，其中 Parser 是简单的递归下降。
