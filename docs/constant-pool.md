# 常量池 v1.2

常量池是本次编译所有函数和全局初始化共享的常量存储表。它不按名字查询，不入作用域栈，也不替代变量、函数等普通符号。由语义阶段解码并检查常量，IR 生成阶段登记，解释器、优化和展示读取。

数据定义在 [interface.hpp](../include/minic/interface.hpp)，接口在 [constant_pool.hpp](../include/minic/constant_pool.hpp)，登记和去重实现已完成，位于 [constant_pool.cpp](../src/constant_pool.cpp)。整个编译器的 generate/run 仍仅有声明，本次已规定它们接入常量池的合同。

## 保存什么

| 字段 | 含义 |
|---|---|
| ConstantEntry.id | 从 0 开始的永久编号，等于 entries 下标 |
| type | 常量的类型快照，例如 int、float、double、char[3] |
| value | 已解码的整数、浮点或字符串；不能为 monostate |
| spelling | 第一次登记的源码原文，供讲解，不参与去重 |
| range | 第一次登记的源码位置；每次使用的位置仍在四元式 locations 中 |
| ConstantPoolData.entries | 按首次登记顺序存储的条目，不删除、不排序 |
| IRProgram.constants | 拥有本 IR 程序的常量池，所有函数共享 |

AST.value 与原文仍保留，便于语义检查和展示。const 变量依然进入普通符号表；枚举项依然有 SymbolEntry。它们作为表达式已知的值需要用于 IR 时，才按结果类型登记入池。

## 去重规则

依据 **类型及规范化值** 去重，不依据源码原文或类型指针地址。实际类型种类、unsigned 和 const/volatile 限定符均参与比较。

- 同为 int 的 10 与八进制 012，语义都解码为 10，复用编号。
- int 的 10、unsigned int 的 10U、double 的 10.0 保留不同条目。
- float 先舍入到 float 精度，再以 double 容器保存，避免相同 float 值产生不同条目。
- 浮点按精确表示去重，不做近似比较；正零与负零分开。
- 字符串按完整解码字节串去重，包含内部零字节；"a\0b" 不会与 "a" 合并。
- 类型使用独立只读快照，调用者修改自己持有的类型对象不能改变池中的键。

当前实现支持 Char/Short/Int/Long 对应的 int64_t 或 uint64_t、Float/Double 对应的 double，以及普通 char[N] 窄字符串。数组长度必须包含末尾零，而 value 中不附加末尾零。

long double 与宽字符串尚缺准确值载体和目标编码模型，明确拒绝入池，不将它们降成 double 或窄字符后假装支持。非法类型、值不匹配、未解码值、NaN/无穷或 float 溢出均抛 invalid_argument 且不改池。整数具体范围、字符编码以及其他目标类型转换由语义阶段检查；池不调用宿主 sizeof 猜测目标布局。

这些异常表示调用阶段未交付合法值，IR 调用者应捕获并转换为原源码处的诊断。内存耗尽等资源异常交给编译总控处理。内部二进制去重键只用于当前进程，不是跨平台序列化格式。

## 操作接口

| 操作 | 参数与返回 | 用途 |
|---|---|---|
| intern(type, value, spelling, range) | 合法解码类型/值、原文、位置 → ConstantId | 登记或复用同值条目 |
| get(id) | 编号 → const ConstantEntry* | 无效编号返回 nullptr；后续插入可能使旧指针失效 |
| size() | 无 → 项数 | 查看去重后的数量 |
| data() | 无 → const ConstantPoolData& | 只读展示 |
| std::move(pool).release() | 无 → ConstantPoolData | 移入 IRProgram.constants，清空登记器后可用于新编译 |
| constant_operand(id) | 合法编号 → "%c0" 等 | 生成正式 IR 操作数，invalid_id 被拒绝 |

IR 生成开始创建一个池，普通字面量、隐式转换后可计算出的值、常量折叠结果都调用 intern，表达式返回 Place{"%c编号", type, true}。不能把一个池的编号交给另一份 IR。编号从首次出现顺序分配，函数结束不会清空池，整个生成结束才移动出去。

## 四元式如何使用

假设常量池如下：

| 编号 | 类型 | 值 |
|---|---|---|
| 0 | int | 5 |
| 1 | char[3] | %d（字节内容，不含末尾零） |
| 2 | int | 0 |

对应示例：

```text
(=, %c0, -, %s2)
(arg, %c1, -, -)
(arg, %s2, -, -)
(call, %s0, 2, -)
(ret, %c2, -, -)
```

这里 call 的 2 是参数数量，属于指令元数据，不是表达式值常量。memberaddr 的偏移同理。池引用可放在值输入位置，不能作为赋值目的地；原有直接字面量演示仍可阅读，但不当作 v1.2 的可执行 IR。

解释器对数值引用直接读取类型和值。窄字符串引用求值为运行时数组到指针转换后的地址，每次 run 为各字符串条目建立独立于调用帧的程序期存储，追加一个终止零；同条目共享存储且不允许修改字面量。数字条目只是值，不用为每个条目分配可寻址变量。验证非法编号/类型时报告 Runtime 错误，不读越界数据。

print_ir 展示指令时同时展示常量池编号、类型和值；导出可执行 IR 必须带常量池，只有一张四元式表不能执行。

## 编译验证

独立检查去重、类型区别、精度、零字节字符串、非法输入、扩容编号及数据归属：

```powershell
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -I include tests/constant_pool_test.cpp src/constant_pool.cpp -o constant_pool_test.exe
.\constant_pool_test.exe
```

运行使用常量池的结构体组装示例：

```powershell
g++ -std=c++17 -Wall -Wextra -Wpedantic -Werror -I include examples/interface_demo.cpp src/constant_pool.cpp -o interface_demo.exe
.\interface_demo.exe
```

测试是独立的行为检查程序，不依赖尚未配置的 GoogleTest；后续 CMake/GoogleTest 集成时应保留这些行为覆盖。
