# 公共数据结构与模块交接约定 v1.5

日期：2026-10-10。依据：README 和 `需求分析/00–05` 文档。

本文件是本组编译器的公共接口基线；可编译定义在 `include/minic/interface.hpp`，全部位于 `minic` 命名空间。后续模块引用该头文件，不再各自定义同名结构体。常量池、符号表、诊断、数组、记录及控制流已有实现；正式词法与语法已实现，范围见 [frontend.md](frontend.md)。实际范围见 [backend-extensions.md](backend-extensions.md)。

v1.1 补齐模块函数合同，见 [modules.md](modules.md)。数据头文件只保留结构体；函数入口分别位于各模块头文件，可统一包含 `minic/modules.hpp`。

v1.2 增加程序共享的常量池：数据定义位于 interface.hpp，登记接口位于 constant_pool.hpp，登记/去重实现位于 src/constant_pool.cpp。详细规则见 [constant-pool.md](constant-pool.md)。IRProgram 现在拥有 constants，正式 IR 的值常量统一使用 `%c<ID>` 引用。

历史 v1.3 补充聚合寻址、数组传参和静态存储合同；本次 v1.5 删除源码指针功能并保留 AST 孩子顺序。IRProgram.globals 扩展为静态存储对象清单，包含 static 局部对象；符号的词法作用域保持不变。

v1.4 将 TypePtr 改为普通指针 `const TypeInfo*`，类型对象由 TypeArena 统一创建和释放。CompilationResult 新增 types 成员保存本次编译的类型；AST 孩子顺序不变。

需求文档中曾提到“已冻结”的 interface.md，但项目此前没有该文件。本版首次落地这些定义，并明确解决旧资料中的歧义。新增枚举代表预留表达能力，不代表已支持该语法；M1/M2/M3/M4 的功能范围仍以 README 为准。C89/C99 的最终验收口径另行统一。

## 1. 各模块使用哪些结构体

`interface.hpp` 现在按阅读顺序分为：公共基础 → 词法分析 → 语法分析 → 语义分析 → 符号表 → 中间代码生成。公共基础包括位置、诊断、编号、类型指针、常量值以及 AST 共用的类别枚举；类型详细定义放在语义分析区，单词和语法树结果分别紧跟各自定义。符号表区包含普通符号、作用域、记录类型和常量池；SemanticResult 内含完整 SymbolTableData，受 C++ 定义顺序约束紧接符号表，仍属于语义分析结果。此顺序用于阅读分类；实际语义分析过程中会调用符号表管理，并非语义结束后才建表。本次仅重排定义和添加分区注释，不改变成员、枚举值或 AST 孩子顺序。

| 数据 | 谁产生/维护 | 谁使用 | 存什么 |
|---|---|---|---|
| SourceLocation / SourceRange | 各阶段 | 全模块、展示层 | 文件、行列、字节位置和源码范围 |
| Token | 词法分析 | 语法分析、Token 展示 | 种别、原文、行列、范围 |
| Diagnostic | 所有阶段 | 编译驱动、诊断展示 | 阶段、级别、位置、中文消息、错误代码 |
| TypeInfo | 语法构造声明类型；语义解析与标注 | 语义、符号表、IR | 基本类型、数组、函数、记录类型及内部地址 |
| ASTNode | 语法构造；语义补充标注 | 语义、IR、AST 展示 | 节点种类、孩子、原文、常量值、类型和符号引用 |
| SymbolEntry | 语义调用符号表模块登记 | 语义、IR、符号表展示 | 变量/形参/函数等的唯一身份及声明信息 |
| MemberEntry / StructEntry | 符号表模块配合语义维护 | 语义、IR | 标签、成员、偏移、大小和对齐 |
| ScopeEntry / SymbolTableData | 符号表模块 | 语义、IR、补全和导出 | 各作用域映射、全部符号、活动作用域栈 |
| Quadruple | IR 生成 | 解释器、优化、IR 展示 | op、arg1、arg2、result 四个字符串 |
| Place | IR 表达式生成 | IR 父节点 | 一个表达式的结果放在哪里，以及结果类型 |
| ConstantEntry / ConstantPoolData | IR 调用 ConstantPool 登记 | IR、解释器、优化、展示 | 常量编号、类型、值、首次拼写和位置 |
| IRFunction / IRProgram | IR 生成 | 解释器、优化和展示 | 函数分组、形参、临时量、入口及全局初始化 |
| LexResult / ParseResult / SemanticResult / IRResult | 对应阶段 | 编译驱动 | 阶段产物与诊断，一致的 ok() 查询 |

## 2. 位置和诊断

`SourceLocation{line, col, offset}`：行列从 **1** 开始，offset 是从 **0** 开始的 UTF-8 字节偏移。列也按字节计数；Tab 算一列；CRLF 算一次换行但占两个字节。界面若要按字符或视觉列展示，应在展示层换算。

`SourceRange{file, begin, end}` 使用左闭右开范围 `[begin, end)`。EOF 的 begin 与 end 相同。合成的隐式转换节点继承原表达式范围；内建声明使用 `file="<builtin>"` 的空范围。

Token / Diagnostic 保留需求资料中的 `line`、`col` 简便字段，它们必须分别等于 `range.begin.line`、`range.begin.col`。AST 和其他条目统一从 range 取得位置，不再重复维护。

`Diagnostic{phase, level, line, col, message, code, range, related}`：

- phase 区分预处理、词法、语法、语义、IR 和运行时。
- level 为 Note / Warning / Error / Fatal；只有 Error / Fatal 阻止下一阶段。
- message 是可读的中文说明；code 是稳定检索标识，如 `SEM_UNDECLARED`。
- related 可定位到先前的声明，便于解释重复声明、类型冲突。

每阶段最多记录 20 条 Error / Fatal；达到上限后结束该阶段，在第 20 条的原始消息后附加“错误过多，停止本阶段”。Fatal 立即停止该阶段。Note / Warning 不计入此上限。无论是否达到上限，只要本阶段有错误，就不执行下一阶段。语义阶段在限额内继续检查其他独立节点，错误子表达式标为 Error 类型，避免衍生报错。

## 3. Token：种别不等于 C 数据类型

`Token{type, lexeme, line, col, range}` 中的 type 是 **TokenType**，表示“这是什么词”；C 数据类型由 **TypeInfo** 表示。

名字统一使用 `ID`、`INT_LITERAL`、`FLOAT_LITERAL`、`CHAR_LITERAL`、`STRING_LITERAL`。文件结束用 `END_OF_FILE`，避免与 C 标准库 `EOF` 宏冲突。不再混用旧文档中的 `FLOAT_LIT` / `STRING_LIT`。

词法成功输出恰好一个末尾 END_OF_FILE，即使输入为空也如此。EOF 原文为空；其他 Token 保留源码拼写，包括字符串的双引号、字符常量的单引号和转义。注释、空白不输出 Token；非法字符产生诊断，跳过后继续扫描。

`main`、`printf`、`scanf` 均为 ID。`AMP` 单独表示 `&`，`AND` 表示 `&&`。后续流程已支持scanf 输入专用取地址和整型按位与；具体含义由 AST 运算符和操作数类型决定。`KW_VOID` 用于函数返回或参数列表。KW_VOLATILE 仅识别后拒绝，不是可用限定符。旧资料“39 类”只是旧清单，不再作为接口总数。

最终词法拼写规则见 [lexer-regex.md](lexer-regex.md) 和 `lexer/patterns.json`，采用 C89 范围：包括 32 关键字、各进制整数、科学计数法、后缀及完整转义。词法种别一次识别齐全，语法/语义功能仍按里程碑实现；早期资料的简单数字规则不再作为最终词法规范。

## 4. TypeInfo：所有模块共用一套 C 类型描述

`TypePtr = const TypeInfo*`，是指向只读类型信息的普通指针。AST、符号表和临时变量借用同一个类型对象，不能分别 delete。不能使用指针地址判断类型相等；语义模块提供递归的结构比较和兼容性规则。

`TypeArena` 用 `std::vector<TypeInfo*>` 保存它 new 出来的对象，析构时逐个 delete；表扩容不改变类型对象地址。`TypeInfo::base` 和 params 中的子类型指针只借用，不递归释放。`create()` 返回可填写成员的 `TypeInfo*`，填好后交给 TypePtr 只读使用。

调用 compile / compile_preprocessed 时，返回的 CompilationResult.types 持有全部编译类型。保留整份 CompilationResult，直到语法树、符号表和 IR 都不再使用；单独复制某张表不会复制类型对象。移动编译结果会转交类型所有权，并保留对象地址。

单独调用 parse、analyze、generate、register_builtins、ConstantPool::intern 或 arithmetic_result 前，先创建 TypeArena 和 TypeArenaScope；管理器必须比借用它的结果活得更久。再次生成已有编译结果的 IR 时，用 `TypeArenaScope scope(compiled.types)`。make_type_info 在当前管理器中创建类型；没有活动 Scope 时抛 logic_error，避免无人释放的对象。run 自行管理执行时的临时类型。

| kind | 有效字段 | 示例 |
|---|---|---|
| Char / Short / Int / Long | is_unsigned、限定符 | unsigned int |
| Void / Float / Double / LongDouble | 限定符 | float |
| Address | base=内部地址对应的数据类型 | 数组传参、输入输出；源码不能声明 |
| Array | base=元素类型，array_length=当前维长度 | int[3][4]：外层 3，base 是长度 4 的数组 |
| Function | base=返回类型，params、variadic、has_prototype | int(int, float)、printf 可变参数签名 |
| Struct / Union / Enum | name=标签拼写，record_id=解析后的身份 | struct Node |
| Named | name=待解析的 typedef 名字 | Size |
| Unknown / Error | 无 | 尚未确定 / 已判定非法 |

每层类型独立保存 const；数组递归保存各维元素类型和长度。array_length 为空表示尚未确定，不表示 0。volatile 字段已删除。

Function.params 保存形参类型，Call.children 保存函数名字和实参。只允许直接函数调用。printf/scanf 内建签名使用内部 Address 表示字符数据，builtin 标记负责专用格式检查。f() 与 f(void) 用 has_prototype 区分。

解析阶段创建声明中写出的类型：`struct T` 可先只有 name，record_id 为 invalid_id；typedef 名可先为 Named。语义按该声明的作用域解析，递归生成新的、已解析的 TypeInfo，写入 AST.type / SymbolEntry.type。禁止修改已有共享类型，更不能仅按标签字符串全局合并类型。后续重声明处理必须复用同一个记录身份。

M1/M2 的数组长度可直接存确定的整型长度。尚未实现的常量表达式数组界限、位域、复杂初始化等需要在相应里程碑增加语法信息；本数据基线不等于已经定义了完整 C 的全部语法。

## 5. ASTNode：一个节点，一种孩子存储方式

| 字段 | 含义 | 填写阶段 |
|---|---|---|
| kind | NodeType：声明、调用、运算等语法种类 | 语法 |
| name | 标识符、运算符、标签或字面量原文 | 语法 |
| value | 解码后的常量值，默认 monostate | 语义 |
| range | 节点覆盖的源码范围 | 语法 |
| declared_type | 声明/显式转换中写出的类型，可含未解析标签 | 语法 |
| type | 语义确定的类型；分析前为空，失败为 Error | 语义 |
| category | None / LValue / RValue / Function | 语义 |
| storage | static / extern 等存储类别，未指定为 None | 语法 |
| symbol_id | 绑定的变量、形参或函数编号 | 语义 |
| scope_id | 此节点所属作用域编号 | 语义 |
| record_id / member_index | 记录身份 / 成员访问的成员下标 | 语义 |
| children | 按下表顺序拥有孩子的 unique_ptr 向量 | 语法；语义可插入转换节点 |

`kind` 与 `type` 分开：`BinaryOp("+")` 是节点种类，float 是该表达式的数据类型。不再用一个 type 字段同时表达两者。左值、右值类别必须由语义标注，赋值、scanf 取地址与成员访问要据此检查。

所有孩子只存 children，不同时维护 left/right、args、kids 三份。孩子不得为空指针；可选的 for 分量使用 Empty 节点。AST 树由 ParseResult.root 唯一拥有，不允许复制；跨模块传引用，转交时 move。`Program` 只是 ASTNode 的别名，根节点必须 kind=Program。

### 孩子顺序合同

| 节点 kind | name / declared_type | children 顺序 |
|---|---|---|
| Program | — | 顶层声明/定义，按源码顺序 |
| FunctionDecl | 函数名；完整 Function 声明类型 | ParamDecl 列表，顺序对应 params |
| FunctionDef | 同上 | ParamDecl 列表，**最后一个是 Block 函数体** |
| ParamDecl | 形参名（原型可为空）；形参声明类型 | 无 |
| VarDecl / MemberDecl | 名字；声明类型 | 无，或 `[初始化表达式]`；M1/M2 MemberDecl 无初始化 |
| StructDef / UnionDef | 标签；可为空代表匿名 | MemberDecl 列表 |
| Block | — | 声明和语句，按源码顺序 |
| ExprStmt | — | `[表达式]` |
| Assign | `=` 或复合赋值运算符 | `[目标表达式, 来源表达式]` |
| BinaryOp | 运算符，如 `+`、`<`、`&&` | `[左操作数, 右操作数]` |
| UnaryOp | `+`、`-`、`!`、`&`（仅 scanf）、`pre++`、`post++` 等 | `[操作数]` |
| Call | name 可保留直接被调函数名 | `[被调表达式, 实参0, 实参1, …]` |
| If | — | `[条件, 真分支]`，或 `[条件, 真分支, 假分支]` |
| While | — | `[条件, 循环体]` |
| For | — | 固定 `[初始化, 条件, 更新, 循环体]`，缺失分量为 Empty |
| Return | — | 无，或 `[返回表达式]` |
| ImplicitCast / Cast | 目标类型；ImplicitCast.type 为目标类型 | `[被转换表达式]` |
| ArrayAccess | — | `[数组表达式, 下标表达式]`；多维用嵌套节点 |
| MemberAccess | 成员名 | `[对象表达式]`；只支持 `s.m`，拒绝 `p->m` |
| DoWhile | — | `[循环体, 条件]` |
| Switch | — | `[控制表达式, 循环体 Block]` |
| Case / Default | — | Case 为 `[常量表达式, 所属语句]`，Default 为 `[所属语句]` |
| Conditional | — | `[条件, 真值表达式, 假值表达式]` |
| Sizeof | 对类型求值时 declared_type 非空 | 对表达式为 `[表达式]`；对类型无孩子 |
| TypedefDecl | 别名；被命名的 declared_type | 无 |
| EnumDef / EnumMember | 标签 / 枚举项名 | EnumDef 为枚举项列表；EnumMember 无或 `[常量表达式]` |
| InitList | — | 初始化项列表，顺序保存；指定初始化为后续扩展 |
| Label / Goto | 标签名 | Label 为 `[所属语句]`；Goto 无孩子 |
| Identifier | 名字 | 无 |
| IntLiteral / FloatLiteral / CharLiteral / StringLiteral | 含引号、转义的原文 | 无 |
| Empty / Break / Continue / Error | — | 无 |

Case/Default 的链可表示连续标签，Block 表示其后的顺序语句。Break/Continue 的目标由 IR 控制上下文维护。C 的 goto 标签属于每函数独立命名空间，后续实现不能放进普通符号表或 struct 标签表。

Function 节点的 ParamDecl 类型和 declared_type.params 必须一致。无名原型形参不进符号表；定义中每个具名形参都登记。函数体最外层 Block 与形参共享 Function 作用域，因此 `int f(int x){int x;}` 应报重复声明；只有更内层的 Block 才可遮蔽 x。

比较签名时，数组形参调整为内部 Address，忽略形参顶层 const；函数体的 const 形参仍不能修改。临时 struct/union 的数组成员可读取，其结果不是可修改左值。

普通窄字符串的 value 保存解码后的字节串（不附终止零），name 保存原文；相邻字符串合为一个 StringLiteral，name 以空格分隔各段完整拼写，语义分别解码再拼接；语义类型是包含终止零的 char 数组，长度为 `value.size()+1`。用于函数实参时，语义插入数组到内部地址的转换。字符常量的 value 用 int64_t 保存解码码值，M1 先遵循本组 char 规则，后续完整 C 对字符常量类型的调整集中在语义模块。最终词法已能识别 L 前缀、完整转义和多字符常量；宽字符/宽字符串的语义存储需在实现中明确，不能直接套用窄字节串的长度规则。

### 例：printf("%d", s)

```text
ExprStmt
└─ Call(name="printf", type=int)
   ├─ Identifier(name="printf", symbol_id=0, category=Function)
   ├─ StringLiteral(name="\"%d\"", value="%d", type=char[3])
   └─ Identifier(name="s", symbol_id=3, type=int, category=LValue)
```

这里有 2 个实参，children.size() 为 3，因为第一个孩子保存被调用的函数名字。

## 6. 符号、标签、成员和作用域

SymbolId / ScopeId / RecordId 是各自 vector 的下标，从 0 分配；invalid_id 表示未绑定。编号不可重用；容器只追加，不删除或排序。vector 扩容可能使元素地址失效，因此 AST 存编号，不存条目指针。

`SymbolEntry` 保存 id、name、kind、type、scope、行列、范围、storage、is_defined、builtin 和可选 offset。函数 type 是完整 Function 类型，返回类型在 type->base，形参在 type->params，不再额外存一份容易不同步的 params。函数原型和定义指向同一 SymbolId；同一作用域的对象重声明是否合法，按里程碑的语言规则检查，不能只看到同名就覆盖条目。

`MemberEntry{name, type, offset, range}` 保存成员声明顺序与字节偏移。`StructEntry` 保存 id、tag、kind、scope、is_complete、members、enumerators、size、alignment 和源码范围。

结构体先分配 RecordId 并登记标签，再处理成员和布局。不完整记录不能直接分配对象，也不能按值包含自身。尚未布局和 offset=0 不同。

布局必须选定目标模型，不能用宿主机器 sizeof。目前 char=1、int=4、float=4、内部地址=8 字节，各自同大小对齐。struct 成员按对齐向上取整后确定 offset，末尾补齐到最大对齐。例如 `{char c; int x;}` 中 c 偏移 0、x 偏移 4，总大小 8，不能简单累计成 5。short=2/2、long=4/4、double/long double=8/8，union 按最大成员大小和对齐布局；完整目标约定见 [backend-completion.md](backend-completion.md)。

`ScopeEntry` 内两张映射：

- symbols：普通名字 → SymbolId（变量、函数、typedef、枚举常量共享普通命名空间）。
- tags：标签名字 → RecordId（struct / union / enum 共享标签命名空间）。

成员名字只在各记录的 members 中查询。`struct node {...}; int node;` 合法，但同一层把 node 先定义为 struct 再定义为 union，应诊断冲突。标签也必须按作用域查询，不能只有一张全局 tag 表；这能处理局部标签遮蔽。

`SymbolTableData` 将所有条目和作用域永久保存，active_scopes 只记录当前查询栈：

1. 分配全局 ScopeEntry，id=0、parent=invalid_id，active_scopes={0}。
2. 进入函数/内层块时追加 ScopeEntry，parent=栈顶，压入新编号。
3. insert 只检查当前层；lookup 沿活动栈从内向外查询。
4. 退出时仅弹 active_scopes，不删除作用域、符号、标签或成员。
5. AST 上保留 scope_id 和 symbol_id；IR 通过编号读取已绑定条目，不按名字重新查找。

作用域进入/退出由 **语义遍历**统一驱动，解析器负责构造语法结构。M3 typedef 消歧若需解析期环境，应使用独立的解析环境，不破坏语义阶段的持久数据。

## 7. 四元式与解释器交接

保持原定 `Quadruple{string op, arg1, arg2, result}` 四字段。内部操作数命名：

| 表示 | 含义 |
|---|---|
| `%s3` | SymbolId=3 的对象或函数；同名变量靠 ID 区分 |
| `%t2` | 当前函数的临时变量，类型见 temporaries |
| `L4` / `user_name` | 内部跳转标号 / 当前函数的用户标签 |
| `%c0` | ConstantId=0 的值常量，类型和值取自 IRProgram.constants |
| `-` | 没有操作数，不能当成数值或名字 |

`Place{name, type, is_const}` 是 gen_expr 的返回值，表示结果所在位置；它不是运行时计算结果。凡是通过成员/数组等间接地址读出的值，必须先生成 load 再作为普通 Place 返回；赋值目标则由 IR 内部另行生成地址。函数和临时量都不能用用户原名作为内部身份。

v1.2 的数值、字符串及折叠结果统一入常量池；Place.name 使用 `%c<ID>` 且 is_const=true。源码原文留在 AST，解释器不再重新解析四元式中的字面量。call 的实参数量、memberaddr 的字节偏移属于指令元数据，仍写十进制文本，不用 `%c`。旧演示及历史四元式表中的直接字面量仅供阅读，不作为新版可执行 IR 格式。

M1 指令合同：

| op | arg1 | arg2 | result | 含义 |
|---|---|---|---|---|
| `=` | 值 | - | 对象或临时量 | 按目标类型复制值 |
| `+ - * /` | 左值操作数 | 右值操作数 | 临时量 | 算术；int/int 向零截断 |
| `< <= > >= == !=` | 左值操作数 | 右值操作数 | int 临时量 | 比较，结果 0/1 |
| `neg / not` | 值 | - | 临时量 | 数值取负 / 逻辑取反 |
| `cvt_i2f` | int 值 | - | float 临时量 | int → float，与旧演示同名 |
| `cvt` | 来源值 | - | 临时量 | 其他转换，目标类型由 temporaries 指定 |
| `label` | - | - | 标号 | 定义跳转位置 |
| `jmp` | - | - | 标号 | 无条件跳转 |
| `jz / jnz` | 条件值 | - | 标号 | 为零 / 非零跳转 |
| `arg` | 已求值的实参 | - | - | 将值复制到本次调用的待传参队列 |
| `call` | 函数 `%sID` | 实参数量（十进制） | 临时量或 - | 按 SymbolId 直接调用函数 |
| `ret` | 返回值或 - | - | - | 返回当前调用者 |
| `addr` | 对象 `%sID` | - | 地址临时量 | 取得普通对象的地址，不读取对象值 |
| `local` | - | - | 局部变量 `%sID` | 每次执行声明时将局部对象重置为未初始化 |
| `%` | 整数左操作数 | 整数右操作数 | int 临时量 | 整数取余，与除法一样向零截断 |

`local` 在变量声明时执行，之后可接初始化赋值。这样循环中重新进入块时，不会把上一轮同一声明的值当作本轮初始值；全局对象在进入初始化序列前统一零初始化。

`&&` / `||` 必须通过跳转实现短路，不可把两侧都算完再做普通二元运算。比较节点类型为 int，操作数的算术提升由语义插入的 ImplicitCast 决定，不能拿“比较结果为 int”去强转两个 float 操作数。

本组解释器选择实参 **从左到右** 求值。先分别求值并在需要时复制到临时量，保留本次求值的值，再连续 emit 本调用的 arg 和 call。嵌套调用必须在外层 arg 序列开始前完成，避免内外参数混入一个队列；后面的实参若会改变变量，前面读出的值必须先快照。call 将参数转移到新调用帧，局部变量和临时量每个调用帧独立，才能支持递归。C 标准未规定一般实参的求值顺序，本组选择只是解释器行为约定。

已实现 zero、load、store、indexaddr、memberaddr、tempaddr、decay 和 offsetaddr，服务数组、成员及输入输出。旧指针运算和间接调用指令已删除，详见 [backend-extensions.md](backend-extensions.md)。

`IRFunction` 按函数保存 symbol_id、形参 SymbolId、临时量类型、四元式和逐指令源码范围。locations.size() 必须等于 quads.size()。`IRProgram` 保存共享常量池、静态存储对象、进入 main 前的初始化序列及其位置/临时量、函数集合、入口 main 的 SymbolId。入口未找到时可以查看 IR，执行时必须诊断。

展示层可把 `%sID` 渲染成人名，同时附编号/作用域；导出可执行 IR 时必须保留唯一名字、常量池、符号/类型表、函数分组及参数信息，单独一张人类可读四元式表不等于完整执行数据。

## 8. 阶段结果与入口

```cpp
LexResult lex(const std::string& source,
              const std::string& filename = "<input>");
ParseResult parse(const std::vector<Token>& tokens);
SemanticResult analyze(Program& program);
IRResult generate(const Program& program, const SymbolTableData& symbols);
```

这四个入口分别在 lexer.hpp、parser.hpp、semantic.hpp、ir.hpp 中声明；lex、parse、analyze 和 generate 均有正式实现。详细合同及符号表、诊断、执行与总控接口见 [modules.md](modules.md)。每阶段填完产物/diagnostics 再调用 ok()；默认构造的结果不代表该阶段已经运行。编译驱动负责按顺序调用并保存所有诊断，不能用一个默认 bool ok 隐瞒错误。

- LexResult：Token 序列 + 词法诊断；错误时仍可展示收集到的 Token。
- ParseResult：AST 根 + 语法诊断；解析恢复可继续收集错误，但只要有语法错误，最终 root 置空。
- SemanticResult：持久符号表数据 + 语义诊断；类型写回传入 AST。失败时可展示已标注部分，不交给 IR。
- IRResult：按函数组织的 IRProgram + IR 诊断；失败时不交给解释器。

IR 生成接收 const AST 与 const 符号表，不登记新源程序符号，不修改已检查的树。它自己的临时量保存于 IRFunction 中。AST、SemanticResult.symbols、IRResult.program 由编译驱动保留到展示/执行结束。

## 9. 相对旧资料统一了什么

| 旧资料口径 | 本版统一口径 | 原因 |
|---|---|---|
| NodeType type 和语义 node->type 混用 | kind 表示节点，type 表示 C 类型 | 避免枚举和类型相互覆盖 |
| left/right、args、kids 并存 | children + 固定孩子顺序 | 每个模块只读一套 AST |
| 数值统统 double | ConstantValue 区分整型、浮点、字符串 | 保留整数精度和字符串解码结果 |
| 退出作用域删掉该层表 | 持久条目 + 活动编号栈 | IR/可视化仍可访问局部声明 |
| 标签全局一张表 | 每作用域一张标签映射 | 支持 C 标签作用域与遮蔽 |
| IR 使用变量原名 | `%sID` 唯一名字 | 不混淆同名局部量 |
| 函数 type/params 各存一份 | 完整 Function TypeInfo | 返回值与形参签名只有一个事实来源 |
| lex 返回 vector 又“同时返回诊断” | LexResult | 返回形式明确 |
| generate 仅返回平序列 | IRResult → IRProgram → IRFunction | 表达函数、调用帧、参数与入口 |
| sizeof/偏移直接累计 | 可选布局字段，按指定目标模型对齐 | 区分未布局和偏移 0，避免宿主依赖 |

原始 Word/PPT 和 ir_demo.cpp 保留为需求及教学历史资料；正式模块以本头文件与本说明为准。以后字段增删或合同变化需更新版本、变更记录并经组内评审；新增枚举不能擅自扩大验收范围。

## 10. 编译使用示例

`examples/interface_demo.cpp` 演示用真实结构体组装 Token、AST、符号表、结构体成员与函数 IR；它不调用编译器入口，也不冒充完整编译测试。

```powershell
g++ -std=c++17 -Wall -Wextra -Wpedantic -I include examples/interface_demo.cpp src/constant_pool.cpp src/type_arena.cpp -o interface_demo.exe
.\interface_demo.exe
```

示例展示 printf 的“被调表达式 + 实参”、不同作用域的同名变量身份、退出作用域后的记录、二维数组与内部结构体地址类型，以及字面量/函数分组的四元式用法。

## 变更记录

- v1.5（2026-10-10）：删除 volatile 字段、C 语言指针、补全接口和 include 读取接口；内部 Address 仅用于数组和输入输出，AST 孩子顺序保持。

- 2026-10-09：公共数据字段保持 v1.2；落实 M1 后续模块，实现 `local` 局部声明重置指令和整数 `%`，支持边界及联调约定见 m1-backend.md。
- v1.2（2026-10-08）：增加 ConstantId、ConstantEntry、ConstantPoolData 及 IRProgram.constants；实现类型和值去重；正式四元式值常量改用 `%c<ID>`。
- v1.1（2026-10-08）：补齐独立模块头文件与函数接口说明；已有四个入口迁入各自模块头文件，数据成员保持不变；新增执行和编译总控结果。
- v1.0（2026-10-08）：首次建立公共数据定义、AST 孩子合同、持久作用域、类型表示、四元式交接和模块结果。
