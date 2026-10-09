# 模块函数接口 v1.2

日期：2026-10-08。结构体及其字段约定见 [interface.md](interface.md)。本文件定义“各模块怎样调用”，全部函数声明位于 `include/minic/`，均附中文注释。

当前已实现常量池、符号表管理、统一诊断、M1 语义检查、IR 生成、解释执行、文本展示及总控。词法和语法入口由其他同学提供；其完成前可以用手动 AST 验证全部后续模块。不要添加返回空结果或无条件成功的占位实现，否则会把“未实现”伪装成“编译成功”。主程序统一包含 `minic/modules.hpp`；某模块可以只包含自己需要的头文件。

## 1. 文件与分工

| 负责人对应任务 | 头文件 | 约定实现文件 | 公共接口 |
|---|---|---|---|
| 词法分析 | lexer.hpp | src/lexer.cpp | lex |
| 语法分析 | parser.hpp | src/parser.cpp | parse |
| 符号表管理 | symbol_table.hpp | src/symbol_table.cpp | SymbolTable、register_builtins（已实现） |
| 统一出错处理 | diagnostic.hpp | src/diagnostic.cpp | DiagnosticEngine（已实现） |
| 语义分析 | semantic.hpp | src/semantic.cpp、src/type_rules.cpp | analyze、same_type / arithmetic_result / can_assign（M1 已实现） |
| 中间代码生成 | ir.hpp | src/ir.cpp | generate（M1 已实现） |
| 常量池（IR 公共支持） | constant_pool.hpp | src/constant_pool.cpp | ConstantPool、constant_operand（已实现） |
| 四元式解释器 | interpreter.hpp | src/interpreter.cpp | run（M1 已实现） |
| 主流程整合 | compiler.hpp | src/compiler.cpp、src/compilation_result.cpp | compile、CompilationResult::ok（已实现，compile 等待词法/语法链接） |
| 文本展示与导出 | display.hpp | src/display.cpp | print_tokens / ast / symbols / ir / diagnostics（已实现） |

词法、语法函数体仍待同学交付；M2–M4 的数组、记录寻址、通用指针及完整 C 规则仍待扩展。M1 支持范围及联调说明见 [m1-backend.md](m1-backend.md)，基础支持模块见 [symbol-table.md](symbol-table.md) 和 [constant-pool.md](constant-pool.md)。没有要求模块继承抽象基类；四个编译阶段用普通函数返回明确的结果，符号表、常量池和诊断收集器通过类封装自己的状态。

## 2. 五个模块怎样交接

```text
源码文本
   │ lex(source, filename)
   ▼ LexResult：tokens + diagnostics
   │ parse(tokens)
   ▼ ParseResult：root + diagnostics
   │ analyze(*root)
   │   内部使用 DiagnosticEngine、SymbolTable 和类型规则
   ▼ 标注后的 AST + SemanticResult：symbols + diagnostics
   │ generate(*root, symbols)
   ▼ IRResult：program + diagnostics
   │ run(program, symbols, input, output)
   ▼ RunResult：exit_code + diagnostics；程序输出直接写入 output
```

调用者必须在进入下一阶段前检查上一阶段的 ok()。Error/Fatal 阻止后续阶段；仅有警告可以继续。公开入口不打印、弹窗或退出进程；源码错误用返回诊断表示，资源耗尽等 C++ 异常可以交给主程序处理。

| 入口 | 参数含义 | 返回值 | 是否修改输入 | 失败行为 |
|---|---|---|---|---|
| lex(source, filename) | 源码文本、来源文件名 | LexResult | 否 | 保留已识别单词并返回词法诊断 |
| parse(tokens) | 已通过词法检查且末尾有 EOF 的序列 | ParseResult | 否 | 收集语法诊断，最终 root 为空 |
| analyze(program) | 新解析的 Program 根节点 | SemanticResult | **是**：填标注并插入转换节点 | 保留部分结果供展示，禁止交给 IR |
| generate(program, symbols) | 同次编译的标注 AST 和符号表 | IRResult | 否 | 报告缺失信息或不支持节点，禁止执行产物 |

parse 输入序列格式错误也要返回诊断，不能越界读取。analyze 对非 Program 根返回 Fatal；同一棵树不重复分析，重新分析时先重新 parse。generate 使用已经绑定的 SymbolId，不能按名字重新查询当前作用域；不符合合同的节点不得静默忽略。

这些入口每次调用使用独立状态，禁止跨调用保留符号表、参数队列或临时变量计数。

## 3. 符号表接口

SymbolTable 构造时借用一个语义 DiagnosticEngine，创建全局作用域 0。收集器必须比符号表活得更久；符号表不能复制。先调用 register_builtins，再开始语义遍历。

| 操作 | 参数 | 返回/作用 | 失败约定 |
|---|---|---|---|
| current_scope() | 无 | 当前作用域编号 | 构造后始终有全局层 |
| enter_scope(kind, range) | Function/Block、源码范围 | 新作用域编号，压入活动栈 | 新建 Global 是调用错误，抛 invalid_argument |
| exit_scope() | 无 | 弹当前层，记录仍保留 | 退出全局返回 false 并报告 Fatal |
| insert(entry) | 待登记的声明 | 新/复用的 SymbolId | 冲突时返回 nullopt 并报告语义错误 |
| lookup(name) | 普通名字 | 从内向外查到的编号 | 未找到返回 nullopt，不自动报错 |
| lookup_current(name) | 普通名字 | 只查当前层 | 同上 |
| symbol(id) | 符号编号 | 条目的只读指针 | 非法编号返回 nullptr |
| set_symbol_offset(id, offset) | 对象编号、字节偏移 | 设置布局字段，返回 bool | 非对象或编号非法则报错，返回 false |
| declare_record(tag, kind, range) | 标签、记录种类、声明位置 | 登记/复用当前层 RecordId | 同层种类冲突则报错，返回空值 |
| complete_record(id, definition) | 编号、完整记录定义 | 校验并填入成员、枚举项、大小和对齐 | 不合法/重复定义返回 false，不覆盖旧数据 |
| lookup_tag(tag) | 标签名 | 从内向外查 RecordId | 未找到返回 nullopt |
| lookup_tag_current(tag) | 标签名 | 只查当前层标签 | 同上 |
| record(id) | 记录编号 | 条目的只读指针 | 非法编号返回 nullptr |
| find_member(id, name) | 记录编号、成员名 | members 中的下标 | 非法/不完整/没有成员返回 nullopt |
| prefix_query(prefix) | 普通名字前缀 | 活动环境中可见符号编号 | 内层同名优先；按名字排序 |
| prefix_query(prefix, scope, cursor) | 前缀、持久作用域、光标位置 | 编译结束后的补全列表 | 非法作用域返回空列表 |
| data() | 无 | 全部数据的 const 引用 | 禁止通过该引用修改表 |
| std::move(table).release() | 无 | 移动出持久数据 | 之后本对象只可析构，不再调用其他方法 |
| register_builtins(table) | 全局环境中的符号表 | 登记 printf/scanf，返回 bool | 非全局/签名冲突报告错误；重复调用不重复插入 |

insert 的 id/scope 由表分配，line/col 从 entry.range.begin 同步，调用者不预先指定身份。M1 同层局部对象重复声明报错；类型兼容的函数原型重声明、原型后定义复用编号，重复定义和签名不兼容报错。插入失败不能覆写原符号或破坏名字映射。后续 extern、typedef 等按里程碑扩展重声明规则。

declare_record 查当前层，外层同名标签不会阻止新标签遮蔽；空标签表示匿名类型，仅分配编号、不加入 tags。定义前先取得编号再处理成员，保证自引用指针可以绑定身份。

complete_record 的 definition.id/tag/kind/scope 必须与旧条目一致。语义模块先检查成员类型并计算布局，填写 size/alignment 和所有成员 offset；符号表校验身份、重复成员及布局完整性后提交定义。失败不能把条目标为完整；枚举项的普通符号登记仍通过 insert，不能只填 enumerators 后假定其名字已经可见。

当前实现检查非零大小、2 的幂对齐、总大小满足对齐、成员类型可存储、偏移未越过对象大小，联合体成员偏移为 0；不计算目标机器的 sizeof、填充或成员区间。枚举完成时还检查每项已绑定本层对应的 EnumConstant。正确布局及成员类型的完整 C 约束由语义遍历负责。

symbol()/record() 返回的元素指针可能因后续插入引起的 vector 扩容而失效；跨修改保存 **编号**，需要时重新取得指针。IR 使用释放后的固定数据时仍不得重新排序容器。

光标补全需要传入源码字节位置，排除该位置之后才首次声明的名字，再执行遮蔽去重。例如内层 x 尚未到声明位置时，外层 x 仍应出现在结果中。内建函数一直可见；原型/定义复用条目保留首次声明位置。M1/M2 为单源文件；M3 多文件预处理需要额外定义光标到展开源码的位置映射，不能直接比较不同文件的 offset。

函数形参和最外层函数体共用一个 Function 作用域；更内层 Block 才进入新的作用域。进入与退出由同一个语义遍历驱动方配对，解析器不操作这份持久表。

## 4. 统一出错处理接口

```cpp
DiagnosticEngine diagnostics(Phase::Semantic); // 默认最多 20 条错误
diagnostics.report(Level::Error, node.range,
                   "变量尚未声明", "SEM_UNDECLARED");
```

每个阶段使用自己的收集器。report 自动设置 phase、line、col，不要求各模块重复拼接位置信息。

- error_count() 只计 Error/Fatal。
- should_stop() 在达到上限或出现 Fatal 时为 true，模块循环必须据此停止。
- diagnostics() 只读查看列表，不转移所有权。
- take_diagnostics() 移走列表并重置计数及 Fatal 标记，用于填写阶段结果。

达到上限时，最后一条错误保留原 message/code，并在 message 后附加“错误过多，停止本阶段”；随后忽略所有新增报告。不能把原错误替换成一条泛化消息，否则错误上限为 1 时会丢掉真正原因。普通源码错误按各阶段恢复策略处理，Fatal 则立即结束该阶段。诊断错误上限参数为 0 是调用错误，抛 invalid_argument。

## 5. 共用类型规则

| 函数 | 判断内容 | 返回约定 |
|---|---|---|
| same_type(left, right) | 已解析类型的结构相等性，含限定符、函数签名和记录身份 | bool；任一空指针为 false |
| arithmetic_result(left, right) | M1 的算术公共类型：char 提升为 int，出现 float 则为 float | TypePtr；不合法返回非空 Error 类型 |
| can_assign(target, source) | M1 相同基本类型、char→int、int/char→float 的赋值兼容性 | bool；未知、非法及不支持组合为 false |

这三个函数不打印、不登记符号、不收集诊断，也不检查左值属性；调用者结合 AST 的 category 和 const 限定符诊断。Error 子表达式向父节点传播时应抑制重复报错。比较与逻辑表达式结果类型为 int，不能把结果类型当成两个操作数的公共算术类型。M1 以外的转换按后续语言规格统一扩展。

实现位于 src/type_rules.cpp。same_type 对 Unknown、Error、未解析 Named 及缺失记录编号返回 false；这是严格的结构相等判断，尚未实现完整 C 的函数类型兼容与形参限定符归一化。算术和赋值只处理 M1 的有符号 char / int / float，忽略顶层 const / volatile；unsigned、short、long、double、指针和数组转换暂不支持。

## 6. 四元式执行接口

IR 的数值、窄字符串和折叠结果通过 ConstantPool.intern 登记为 `%c<ID>`，generate 结束时将池移入 IRProgram.constants。解释器通过同次编译的池读取已解码值。常量池接口、精度和字符串存储规则见 [constant-pool.md](constant-pool.md)。

```cpp
RunResult run(const IRProgram& program, const SymbolTableData& symbols,
              std::istream& input, std::ostream& output,
              const RunOptions& options = {});
```

input/output 由主程序拥有，分别承接 scanf/printf。测试时可以传入字符串流；保存程序输出时可以传入文件流。run 不关闭流，也不再执行前端编译。

RunOptions.max_steps 默认 1,000,000，包含全局初始化及所有函数的指令；max_call_depth 默认 1024，包含 main；0 表示对应限制不启用。达到限制后报告 Runtime 错误并停止。每次执行建立独立全局存储、调用帧和参数队列。

执行前检查入口、符号/临时量引用、指令种类、函数签名、指令和位置数组等结构合同，失败时不执行代码。M1 入口约定无参数 int main；先全局初始化再执行入口。除零、输入失败或运行时越界等在执行中诊断，停止后保留已写出的输出。

RunResult 保存 executed_steps、diagnostics 和可选 exit_code。正常完成时 exit_code 有值；程序 main 返回 3 仍表示执行成功，run.ok() 为 true。解释器失败时退出码为空，run.ok() 为 false，由主程序选择自身失败码。

## 7. 总控与命令行的对应

compile(source, filename, target) 保存所有已执行阶段的产物并汇总诊断，不读取文件、不打印、不运行代码。CompilationResult 的阶段字段采用 optional，以区分“未运行”和“已运行但失败”。

| 用户命令 | CompileTarget | 使用的结果 |
|---|---|---|
| minic tokens | Tokens | lexical.tokens |
| minic parse | Parse | syntax.root、诊断 |
| minic symbols | Check | semantic.symbols |
| minic check | Check | diagnostics、semantic.ok() |
| minic ir | IR | ir.program 和 semantic.symbols |
| minic run | IR | 编译成功后额外调用 run |

CompilationResult::ok() 必须同时满足：

1. 请求阶段及它之前的所有阶段结果存在。
2. 每个已执行阶段的 ok() 为 true，汇总诊断没有 Error/Fatal。
3. 没有执行超出 target 的阶段；Tokens 只词法，Parse 到语法，Check 到语义，IR 到生成。

构造但未执行的结果不算成功。词法失败时 lexical 存在，其余为空；语法失败时 syntax 存在但 root 为空；语义失败时 semantic 存在而 ir 为空。诊断在各阶段结果内保留原列表，汇总列表保存副本，不允许展示层重复累加。

ParseResult 拥有 AST，SemanticResult 拥有符号表，IRResult 拥有中间代码；保留 CompilationResult 到展示/执行结束。它包含 unique_ptr，移动而不复制。run 接收的 IR 和 symbols 必须来自这同一份结果。

## 8. 展示和导出

print_tokens、print_ast、print_symbols、print_ir、print_diagnostics 均向调用者提供的 ostream 输出，只读产物。打印函数不会重新编译，也不会关闭流。输出流失败由调用者检查流状态；文本展示不把文件 I/O 问题当成用户 C 程序诊断。

IR 显示可用符号表补充用户名字，但同时保留 `%sID`，保证同名变量可区分。文本格式服务课堂演示；正式 JSON 导出及序列化格式后续另行冻结，当前不承诺文本能直接反序列化为可执行 IR。

## 9. 使用与验证

[compile_and_run.cpp](../examples/compile_and_run.cpp) 给出模块实现后的总控调用示例。当前可做声明层的编译检查：

```powershell
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -I include -fsyntax-only examples/compile_and_run.cpp
```

这验证头文件、函数参数、返回类型和流程调用是否兼容，不验证编译器行为。compile_and_run.cpp 还需要词法/语法实现才能链接。M1 后续模块已有实际行为测试：运行 `tools/test_backend.ps1`，或使用根目录 CMakeLists.txt 和 CTest。compiler_flow_test.cpp 中的词法/语法替身只检查总控调用顺序，不属于正式扫描器或解析器。

v1.1 将四个已有入口迁入各模块头文件；v1.2 新增常量池并在 IRProgram 中保存数据。原来只含 interface.hpp 的调用代码如需调用模块函数，改含对应头文件或 modules.hpp；interface_demo.cpp 从 v1.2 起需同时链接 src/constant_pool.cpp。
