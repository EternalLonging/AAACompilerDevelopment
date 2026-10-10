# 符号表与统一报错：已实现功能

本部分可以独立编译运行，头文件中的 `SymbolTable`、`register_builtins` 和 `DiagnosticEngine` 均已有函数体。语义模块现在可以直接调用它们建表、查名字和报错。注释采用简短中文，详细使用规则放在本文和 [modules.md](modules.md)。

## 1. 文件与功能

| 文件 | 已实现的功能 |
|---|---|
| `src/symbol_table.cpp` | 建立/退出作用域，登记/查询普通符号，合并兼容函数声明，设置对象偏移 |
| 同上 | 登记标签，完成结构体/联合体/枚举记录，查询成员，登记 printf / scanf |
| 同上 | 当前可见名字的前缀查询，指定作用域和源码光标位置的补全查询 |
| `src/diagnostic.cpp` | 收集说明、警告、错误和致命错误，保存行列和关联位置，限制错误数量 |
| `src/type_rules.cpp` | 比较类型，计算扩展数值的算术结果类型，判断赋值类型是否兼容 |
| `examples/symbol_table_demo.cpp` | 展示同名变量遮蔽、退出后恢复查询、前缀查询和重复声明报错 |
| `tests/symbol_table_test.cpp` | 验证作用域、函数声明、标签/成员、补全、内建函数、诊断和类型规则 |

## 2. 怎么建表

1. 创建 `DiagnosticEngine(Phase::Semantic)`，再创建 `SymbolTable`。构造时自动建立全局作用域，编号为 0。
2. 调用 `register_builtins(table)`，登记 `int printf(const char*, ...)` 和 `int scanf(const char*, ...)`。
3. 用 `insert(entry)` 登记形参、函数、类型别名或枚举项；对象声明使用 `declare_object(entry)`，合并兼容的全局声明和 extern，普通局部对象内部仍使用 insert。返回空值说明失败，原因已放入诊断列表。
4. 进入函数时调用 `enter_scope(Function, range)`；形参与最外层函数体放在这一层。嵌套花括号才建立 Block 层。
5. 用 `declare_record` 取得结构体等记录的编号，再用 `complete_record` 提交检查后的成员和布局。枚举项要先通过 `insert` 登记普通名字。
6. 退出时调用 `exit_scope()`。退出只改变查询环境，已经登记的数据会保留，供 IR 和展示层使用。

```cpp
minic::TypeArena types; // 保存类型对象，符号表用完后统一释放。
minic::TypeArenaScope type_scope(types); // 指定当前创建类型的存放位置。
minic::DiagnosticEngine diagnostics(minic::Phase::Semantic);
minic::SymbolTable table(diagnostics);
minic::register_builtins(table);

auto* type = minic::make_type_info();
type->kind = minic::TypeKind::Int;

minic::SymbolEntry entry;
entry.name = "count";
entry.type = type;
entry.range = {"demo.c", {1, 5, 4}, {1, 10, 9}};
auto id = table.insert(entry);
if (id) {
    auto found = table.lookup("count");
    // found 保存 count 的符号编号。
}
```

符号编号、记录编号和作用域编号都对应各自表的下标，只追加、不重新排序。同层重复变量会报错，内层同名变量可以遮蔽外层。兼容的函数原型和后续定义复用同一个编号，并保留首次声明的位置。保存编号即可；不要跨插入保存 `symbol()` 或 `record()` 返回的指针，因为表扩容可能使指针失效。

`register_builtins` 可重复调用。已有相同签名、尚未定义且非 static 的原型会复用编号并补上内建标记；已有用户定义或签名冲突则报错，不覆盖用户记录。登记前会检查两个名字，名字冲突时不会只登记一个函数。

## 3. 怎么报错

调用 `diagnostics.report(Level::Error, range, "变量未声明", "SEM_UNDECLARED")` 即可，阶段和行列会自动填写。重复声明的报错会带上原声明的位置。

默认最多记录 20 条 Error/Fatal。达到上限时，在最后一条实际错误后加上“错误过多，停止本阶段”，之后忽略新增报告。Fatal 立即停止；Warning/Note 不计入错误数量。调用方在循环中检查 `should_stop()`，结束时用 `take_diagnostics()` 取走诊断并重置收集器。

## 4. 编译与运行

在项目根目录使用支持 C++17 的 g++。以下测试不依赖额外测试库，生成文件放在过程目录：

```powershell
New-Item -ItemType Directory -Force .document-work | Out-Null

g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -I include tests/symbol_table_test.cpp src/symbol_table.cpp src/diagnostic.cpp src/type_rules.cpp -o .document-work/symbol_table_test.exe
& .document-work/symbol_table_test.exe

g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -I include examples/symbol_table_demo.cpp src/symbol_table.cpp src/diagnostic.cpp src/type_rules.cpp -o .document-work/symbol_table_demo.exe
& .document-work/symbol_table_demo.exe
```

测试成功时输出“符号表、诊断与类型规则：5 组测试全部通过”。示例故意登记一次重复变量，展示的预期结果为：

```text
全局 count 编号：2
函数内 count 编号：3
退出函数后的 count 编号：2
前缀 pri 的查询结果：printf
demo.c:4:9 同一作用域中的声明冲突：count
```

## 5. 当前范围

符号表现已接入数组、结构体、typedef、enum、函数和对象指针的 `analyze` 遍历，并通过语义→IR→解释执行联动测试。词法/语法尚待同学交付，目前可运行手动 AST 示例，不能直接编译 C 源文件。见 [backend-extensions.md](backend-extensions.md)。

类型比较包含指针、数组、函数和记录身份，数组/函数形参会调整为指针。declare_object 合并兼容全局对象声明、暂定定义和 extern，两个带初始化的定义会冲突；普通局部对象和 typedef 同层重复仍拒绝。扩展数值规则已覆盖 short/long/unsigned/double/long double，保留原 MiniC 的 char/int/float 隐式缩窄限制。完整 C 原型兼容、链接和重声明规则尚未全部实现。

结构体布局由语义模块计算后提交。本部分检查身份、名字、可存储的成员类型、必要布局字段和基本偏移合法性，不自行假定目标机器的类型大小，也不替代完整布局校验。补全的光标位置按单源文件字节偏移比较，多文件映射留给预处理阶段。

语义阶段实际建表方式：对象使用 declare_object；形参/typedef/枚举常量使用 insert；函数使用 insert 合并兼容声明；struct/union/enum 先 declare_record，再 complete_record。static 局部对象仍保留原块作用域，存储由 IR 静态对象清单单独决定。函数内的 goto 标签使用语义阶段独立标签集合，不与类型标签表混用。常量值由 ConstantPool::intern 单独登记，不拆成变量表和函数表。
