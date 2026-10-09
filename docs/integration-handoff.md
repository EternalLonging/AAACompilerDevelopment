# 源码联调交接

当前先按同学交付的类 C89 文法联调：同一块先声明再写语句，for 初始化用表达式，if/else/while/for/do/switch 控制体带花括号。后端更宽的支持保留；本套样例不要求扩大当前语法范围。

## 入口与结果归属

源码夹具位于 `tests/integration/`，`manifest.json` 为唯一预期清单。正常用例记录输入、完整输出和 main 返回值，错误用例记录最早失败阶段、已固定的错误代码或关键词。不以 GCC 对错误样例的行为作为项目判据，也不运行其中的未定义行为。

`tests/integration_driver.cpp` 是测试入口：调用 `compile_preprocessed`，成功后用受限解释器执行。它不实现 lex/parse，不使用测试替身冒充前端。被测程序输出写 stdout，诊断和测试标记写 stderr；驱动退出 0 表示成功、1 表示项目诊断失败、2 表示夹具/驱动故障。main 的返回值另用 `[fixture_exit=N]` 记录，避免与驱动故障混淆。指令上限为 10,000，调用深度上限为 128，Python 单用例限时 20 秒。

测试头文件读取器只用于夹具内的本地头文件，当前宏样例为单层头文件加重复包含守卫；它不提供系统头文件或完整系统路径搜索。printf/scanf 由项目内建登记，源码不包含 stdio.h；GCC 参考验证会强制包含 stdio.h，以取得宿主声明。

## 正式 AST 必须遵守的约定

公共结构与完整孩子顺序以 `include/minic/interface.hpp` 和 `docs/interface.md` 为准。以下是本套夹具最容易出错的对应关系；树形示意省略源码位置和非关键节点。

### 02_input_sum.c

`int main(void)` 的函数签名为 Function(base=Int, params=空)，`void` 不是一个 ParamDecl。函数体是最后一个孩子。

```text
Program
└─ FunctionDef "main"
   └─ Block
      ├─ VarDecl "n" : Int
      ├─ VarDecl "i" : Int
      ├─ VarDecl "sum" : Int
      ├─ ExprStmt
      │  └─ Call "scanf"
      │     ├─ Identifier "scanf"
      │     ├─ StringLiteral 原文="%d"（含引号）
      │     └─ UnaryOp "&" → Identifier "n"
      ├─ ExprStmt → Assign "=" [Identifier "sum", IntLiteral "0"]
      ├─ For
      │  ├─ Assign "=" [Identifier "i", IntLiteral "1"]
      │  ├─ BinaryOp "<=" [Identifier "i", Identifier "n"]
      │  ├─ UnaryOp "post++" → Identifier "i"
      │  └─ Block → ExprStmt → Assign "+=" [Identifier "sum", Identifier "i"]
      ├─ ExprStmt → Call "printf" [Identifier "printf", StringLiteral, Identifier "sum"]
      └─ Return → IntLiteral "0"
```

For 的四个孩子固定为初始化、条件、更新、循环体；省略分量要放 Empty，不能删掉孩子。Call 的第一个孩子始终是被调表达式，实参从第二个孩子开始。Assign 的目标必须是表达式节点，不能只把目标名字放 name。

### 05_arrays.c 与 06_struct.c

`int a[2][3]` 表示 Array(2, base=Array(3, base=Int))。`a[i][j]` 表示 ArrayAccess(ArrayAccess(Identifier a, Identifier i), Identifier j)，不能把两个下标压进同一个节点。

`struct Pair { int x; int y; };` 产生 StructDef("Pair")，孩子为两个 MemberDecl。`struct Pair p={1,2}` 产生 VarDecl("p", declared_type=Struct(name="Pair"))，唯一孩子为 InitList(IntLiteral 1, IntLiteral 2)。`p.x` 是 MemberAccess(name="x", children=[Identifier p])。

`change` 的声明类型为 Function(base=Struct Pair, params=[Struct Pair])；孩子是 ParamDecl("p") 和 Block。类型标签交给语义阶段绑定 RecordId；解析器不预先算成员偏移。返回值与函数实参保留表达式树，值拷贝由后端处理。

### 08_callback.c

```text
int (*fn)(int)
→ declared_type = Pointer(base=Function(base=Int, params=[Int]))

fn(x)
→ Call(children=[Identifier "fn", Identifier "x"])

apply(fn, 6)
→ Call(children=[Identifier "apply", Identifier "fn", IntLiteral "6"])
```

函数指针类型不是 Function 的返回类型 Pointer；两者不可交换。`int apply(int (*fn)(int), int x)` 的函数签名保存 [Pointer(Function), Int]，对应两个 ParamDecl，然后才是函数体 Block。

### 10_typedef_enum.c 与 13_macros.c

typedef 用 TypedefDecl(name="Count", declared_type=Int)，后续 `Count n` 的声明类型用 Named(name="Count")，交给语义阶段解析。语法阶段需要自己的轻量 typedef 作用域分类，不能等整棵 AST 建好后才识别类型名，也不能一次性重分类全部 ID。Label/Goto 的 name 保存标签，不与类型标签共用表。

宏样例先预处理，HASH/HASH_HASH 及 include 指令不进入普通 AST。展开后的主体相当于 `int value3=3+4; printf("macro = %d %s\n", value3, "3");`。正式总控用 `compile_preprocessed`；原 compile 和现有 minic 命令行仍是直接编译入口，本套自动验收不假定命令行已自动启用预处理。

## 联调顺序

1. 先用 01、02 检查 Token 原文、EOF、函数体、表达式优先级和输入输出。
2. 接 03、04 检查调用、递归、返回和控制流。
3. 接 05–12、15、16 检查声明符、初始化、类型及符号绑定。
4. 接 13、14 检查预处理入口和短路副作用。
5. 最后跑全部错误用例，确认最早失败阶段正确，失败后不继续执行。

接入阶段允许先报告尚未完成的样例，但不能修改清单把失败伪装成通过。若决定改变语言约定，应一起改文法、样例、清单与说明。

## 当前验证与待办

准备阶段已核实清单、实际预处理和词法规则；16 个正常样例的输出通过 GCC 参考执行核实。另用同学的上下文预测核心验证 30 个非词法/预处理错误样例的接受或拒绝；typedef 样例明确给出类型名位置，这不验证完整作用域适配器。

正式 lexer.cpp/parser.cpp 尚未交付，**32 个用例尚未通过本项目真实源码全链路验收**。驱动的两种入口已严格检查 C++17 接口与语法；正式源码执行将由前端交付后的 source_integration 测试验证。准备检查和 GCC 参考通过不能替代该结果。
