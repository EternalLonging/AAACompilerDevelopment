# reduce() 语义结果合并动画

直接用浏览器打开 [reduce-actions.html](reduce-actions.html)，不需要启动服务或安装依赖。默认展示形参续；左侧可按非终结符或 P 编号搜索。

## 页面展示什么

- 覆盖当前 110 条产生式，以及 run() 创建 Program 根的收尾过程。
- 204 个固定场景、1,096 个动画步骤；可以切换同一产生式的有尾表、空尾表等情况。
- 每次动作展示右部 Value、工作根 n、返回值 out、nodes 和 children 的区别。
- 列表保持源码逆序；装入 Block、FunctionDef、Call 或 Program 的孩子时恢复源码顺序。
- 节点保留编号，便于区分创建新节点、沿用原节点、补类型及连接孩子。
- 支持播放、暂停、单步、进度拖动、阶段跳转、树图缩放和节点字段查看；手机也可使用。

左右方向键单步，空格播放或暂停。点击节点可以查看 declared_type、children 和 Token 范围。

## 对应代码和边界

生成器从 `src/语法分析/predictive_table.hpp` 读取项目预测表，对内置片段实际执行查表展开和归约，再按 `parser.cpp` 的语义动作生成动画快照。页脚记录 parser.cpp 的 SHA-256 前 12 位。

这是固定场景的教学模型，不是可编辑的完整 C 编译器。片段只要求语法有效，部分名字及控制语句故意不满足语义检查。动画只画与本次动作有关的节点；大树折叠部分深层连接，可点击节点查看直接孩子。节点编号在各自片段内有效。

右部卡片保留动作前的读取快照，不模拟 `std::move` 后容器的内部状态。finish() 的树高检查没有拆成单独动画。类型补全展示逻辑结果，不把共享 TypePtr 的地址别名展开。最终创建 Program 时不再对 values 执行 resize/push。

## 重新生成与验证

这些文件放在仓库的 `docs/learning/` 中，生成器据此定位仓库根目录。修改语义动作后要同步更新模型；仅重新读取预测表不能自动翻译新的 C++ 动作。

```sh
python docs/learning/build-reduce-actions.py
node docs/learning/check-reduce-actions.cjs
```

浏览器验证需要 Node.js、Playwright 和 Microsoft Edge。默认从本地 Node 模块解析 Playwright；若使用外部依赖目录，可把 `PLAYWRIGHT_MODULE_ROOT` 设为包含 `playwright` 的 node_modules 路径。

原生 AST 对照需要先编译 `verify-reduce-native.cpp` 与当前 parser.cpp，再链接项目构建的两个静态库。例如 MinGW 环境：

```sh
g++ -std=c++17 -finput-charset=UTF-8 -Iinclude docs/learning/verify-reduce-native.cpp src/语法分析/parser.cpp build/ninja/libminic_compiler.a build/ninja/libminic_backend.a -o docs/learning/verify-reduce-native.exe
python docs/learning/check-reduce-native.py
```

此对照只调用 lex()/parse()，不进入语义分析，逐节点比较四组固定片段的 AST 种类、名字和孩子顺序。验证会在当前目录生成临时 `.c` 文件和编译程序，交付时不提交这些文件。

本次验证：110/110 产生式覆盖；204 场景、1,096 步渲染及栈卡片检查通过；播放暂停、筛选、节点查看、390px 手机宽度检查通过，无页面脚本错误；四组片段的 AST 与当前 C++ 实现一致。
