"""生成其余中文设计表，并用真实编译结果核对示例、转换规则和交付文件。"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

from generate_lexer_tables import check_workbook
from generate_parser_tables import build_payload

ROOT = Path(__file__).resolve().parents[1]
DESIGN = ROOT / '实验设计'
SAMPLE = DESIGN / '公共示例/表格示例.c'
SEM = 'src/语义分析/analyzer.cpp'
IR = 'src/中间代码生成/ir.cpp'
SYM = 'src/符号表管理/symbol_table.cpp'
INTERFACE = 'include/minic/interface.hpp'


def text(name):
    """读取 UTF-8 源文件，统一换行为 LF。"""
    return (ROOT / name).read_text(encoding='utf-8')


def ref(name, needle):
    """定位来源片段，返回仓库相对路径与行号。"""
    for line, value in enumerate(text(name).splitlines(), 1):
        if needle in value:
            return f'{name}:{line}'
    raise ValueError(f'来源片段已变化：{name} / {needle}')


def sheet(name, title, context, headers, rows, widths, notes=()):
    """整理一张工作表的数据及排版参数。"""
    if not rows or any(len(row) != len(headers) for row in rows):
        raise ValueError('表格为空或列数不一致：' + name)
    return dict(name=name, title=title, context=context, headers=headers,
                rows=rows, widths=widths, notes=list(notes))


def run_driver(driver, source, mode='inspect'):
    """调用真实前端，返回 JSON；进程失败或输出无效时中止。"""
    result = subprocess.run([str(driver), mode, str(source), str(source), '0'],
                            capture_output=True, encoding='utf-8', check=True)
    return json.loads(result.stdout)


def diagnostic_rows(name, prefix):
    """列出源码中的诊断代码及所在行，保留内部兼容分支的诊断。"""
    rows = []
    for line, value in enumerate(text(name).splitlines(), 1):
        for code in re.findall(r'"(' + prefix + r'_[A-Z_]+)"', value):
            rows.append([code, value.strip(), f'{name}:{line}'])
    return rows


def field_rows(names):
    """读取公共结构的字段声明与已有中文注释，避免手抄字段。"""
    source = text(INTERFACE)
    rows = []
    for name in names:
        body = re.search(r'\bstruct ' + name + r' \{\n(.*?)\n\};', source, re.S)
        if not body:
            raise ValueError('公共结构已变化：' + name)
        first_line = source[:body.start(1)].count('\n') + 1
        for line, value in enumerate(body[1].splitlines(), first_line):
            field = re.match(r'    ([^;]+); // (.+)', value)
            if field:
                declaration, meaning = field.groups()
                rows.append([name, declaration.strip(), meaning, f'{INTERFACE}:{line}'])
    return rows


# 规则只描述当前源码允许的构造；旧兼容 AST 和枚举不据此变成语言功能。
SEMANTIC_RULES = [
    ('声明与绑定', '名字', '使用前先声明；从当前作用域向外查找，内层可遮蔽外层。', 'identifier(', 'SEM_UNDECLARED'),
    ('声明与绑定', '变量', '对象类型须可布局；省略数组长度时从列表或字符数组字符串初值推导。', 'void variable(', 'SEM_DECL_TYPE'),
    ('声明与绑定', 'const', 'const 变量必须初始化；数组元素与结构成员继承 const。', 'void variable(', 'SEM_CONST_INIT / SEM_LVALUE'),
    ('声明与绑定', 'struct', '按已声明标签绑定记录身份；逐成员计算对齐与布局后完成记录。', 'void record_definition(', 'SEM_TAG / SEM_LAYOUT'),
    ('初始化', '列表', '数组及结构体初值不能超出容量；标量列表必须恰好一项。', 'void initialize(', 'SEM_INIT_COUNT'),
    ('初始化', '全局初值', '全局初值必须满足实现的常量初始化判定；不能依赖普通变量运行求值。', 'bool constant_initializer(', 'SEM_GLOBAL_INIT'),
    ('初始化', 'char 数组', '显式长度须不少于解码字节数，可恰好装满而无末尾零；省略长度时按字节数加一推导。', 'void initialize(', 'SEM_INIT_COUNT'),
    ('表达式', '四则与取余', 'char 提升到 int；含 float 的算术采用 float；取余需要整型。', 'void binary(', 'SEM_OPERANDS'),
    ('表达式', '比较与逻辑', '比较和逻辑结果为 int；逻辑短路交给 IR 控制流生成。', 'void binary(', 'SEM_OPERANDS'),
    ('表达式', '赋值', '目标须为可修改左值；数值类型按转换规则检查，同记录类型可整体赋值。', 'void assignment(', 'SEM_LVALUE / SEM_ASSIGN_TYPE'),
    ('表达式', '下标', '对象须为数组或数组形参内部地址；下标须整型，运行时另检查边界。', 'case NodeType::ArrayAccess:', 'SEM_INDEX'),
    ('表达式', '成员', '对象须为 struct，成员必须存在；保存记录编号、成员序号并传播限定符。', 'case NodeType::MemberAccess:', 'SEM_MEMBER'),
    ('函数', '调用', '仅直接函数名调用；普通实参数量、类型与签名一致，数组参数作内部地址转换。', 'void call(', 'SEM_CALL / SEM_ARGUMENT_COUNT / SEM_ARGUMENT_TYPE'),
    ('函数', '返回', '返回值与函数类型相容；非 void 函数须通过当前结构化返回路径检查。', 'case NodeType::Return:', 'SEM_RETURN / SEM_MISSING_RETURN'),
    ('控制', '条件', 'if、while、for 的非空条件须是数值。', 'void require_condition(', 'SEM_CONDITION'),
    ('控制', '循环跳转', 'break 与 continue 只能位于循环体内。', 'case NodeType::Break:', 'SEM_LOOP'),
    ('输入输出', '格式', '格式串必须为字面量，仅支持 %d、%f、%c、%s、%%，数量和类型均核对。', 'void call(', 'SEM_FORMAT'),
    ('输入输出', '寻址', '& 仅用于 scanf 的可写数值输入参数；%s 采用字符数组内部地址。', 'void unary(', 'SEM_ADDRESS / SEM_FORMAT'),
]

# 每项保留向真实编译器提交的源码及期望诊断，表格不是假定的检查结果。
CASES = [
    ('未声明名字', 'int main(){return missing;}', 'SEM_UNDECLARED'),
    ('同层重名', 'int main(){int x;int x;return 0;}', 'SYM_DECL_CONFLICT'),
    ('const 赋值', 'int main(){const int x=1;x=2;return 0;}', 'SEM_LVALUE'),
    ('const 未初始化', 'int main(){const int x;return 0;}', 'SEM_CONST_INIT'),
    ('数组长度未确定', 'int main(){int a[];return 0;}', 'SEM_DECL_TYPE'),
    ('过多初值', 'int main(){int a[1]={1,2};return 0;}', 'SEM_INIT_COUNT'),
    ('全局非恒定初值', 'int x=1;int y=x;int main(){return 0;}', 'SEM_GLOBAL_INIT'),
    ('浮点下标', 'int main(){int a[2];return a[1.0];}', 'SEM_INDEX'),
    ('调用参数数量', 'int f(int x){return x;}int main(){return f();}', 'SEM_ARGUMENT_COUNT'),
    ('返回值缺失', 'int main(){return;}', 'SEM_RETURN'),
    ('返回路径缺失', 'int main(){int x=1;}', 'SEM_MISSING_RETURN'),
    ('循环外跳转', 'int main(){break;return 0;}', 'SEM_LOOP'),
    ('普通取地址', 'int main(){int x=1;&x;return 0;}', 'SEM_ADDRESS'),
    ('格式类型不匹配', 'int main(){printf("%d",1.0);return 0;}', 'SEM_FORMAT'),
]

# 这是当前源语言可生成的四元式。内部旧位运算等兼容指令不列为源码支持。
OPERATIONS = [
    ('=', '值', '—', '临时量或对象', '复制值、保存求值快照'),
    ('+', '左值输入', '右值输入', '临时量', '数值加法'),
    ('-', '左值输入', '右值输入', '临时量', '数值减法'),
    ('*', '左值输入', '右值输入', '临时量', '数值乘法'),
    ('/', '左值输入', '右值输入', '临时量', '数值除法，执行检查除零'),
    ('%', '整型输入', '整型输入', '临时量', '整型取余，执行检查除零'),
    ('<', '左值输入', '右值输入', 'int 临时量', '小于比较'),
    ('<=', '左值输入', '右值输入', 'int 临时量', '小于等于比较'),
    ('>', '左值输入', '右值输入', 'int 临时量', '大于比较'),
    ('>=', '左值输入', '右值输入', 'int 临时量', '大于等于比较'),
    ('==', '左值输入', '右值输入', 'int 临时量', '相等比较'),
    ('!=', '左值输入', '右值输入', 'int 临时量', '不等比较'),
    ('neg', '数值', '—', '临时量', '一元负号'),
    ('not', '数值', '—', 'int 临时量', '逻辑非'),
    ('cvt_i2f', 'int 值', '—', 'float 临时量', 'int 转 float'),
    ('cvt', '来源值', '—', '目标类型临时量', '其他合法隐式转换；目标类型查临时量表'),
    ('label', '—', '—', '标号', '定义跳转标号'),
    ('jmp', '—', '—', '标号', '无条件跳转'),
    ('jz', '条件值', '—', '标号', '条件为零则跳转'),
    ('jnz', '条件值', '—', '标号', '条件非零则跳转'),
    ('local', '—', '—', '局部符号', '建立本次调用的局部对象'),
    ('zero', '—', '—', '对象符号', '将指定聚合存储初始化为零'),
    ('addr', '符号', '—', '地址临时量', '获取对象内部地址'),
    ('indexaddr', '数组地址', '整型下标', '元素地址', '普通数组寻址'),
    ('offsetaddr', '数组形参地址', '整型下标', '元素地址', '数组形参寻址'),
    ('memberaddr', '结构体地址', '成员字节偏移', '成员地址', '成员寻址；偏移属于元数据'),
    ('tempaddr', '聚合临时值', '—', '地址临时量', '临时聚合寻址'),
    ('decay', '数组地址', '—', '内部首元素地址', '数组到传参或字符串地址的转换'),
    ('load', '地址', '—', '值临时量', '读取存储值'),
    ('store', '值', '—', '地址', '写入存储值'),
    ('arg', '实参值或地址', '—', '—', '压入已求值实参'),
    ('call', '函数符号', '实参数量', '返回临时量或 —', '直接调用；参数数量属于元数据'),
    ('ret', '返回值或 —', '—', '—', '返回调用方'),
]

SYMBOL_RULES = [
    ('全局层', '构造时建立编号 0；不能再进入新的 Global，也不能退出全局。', 'SymbolTable::SymbolTable('),
    ('函数层', '形参与函数最外层体在同层；内嵌花括号再建立块层。', 'SymbolTable::enter_scope('),
    ('普通名字', 'lookup 从 active_scopes 栈顶向外查；lookup_current 仅查本层。', 'SymbolTable::lookup('),
    ('标签名字', 'struct 标签使用独立 tags 名字表；与普通对象名分开。', 'SymbolTable::lookup_tag('),
    ('退出作用域', '只弹出查询栈，symbols、records、scopes 的条目均保留。', 'SymbolTable::exit_scope('),
    ('同层冲突', '普通局部对象同层重名拒绝；内层同名可遮蔽外层。', 'SymbolTable::insert('),
    ('全局对象', '兼容全局对象声明复用编号；两个已带初始化定义冲突。', 'SymbolTable::declare_object('),
    ('函数兼容接口', 'insert 可合并兼容函数声明；当前源语言仍拒绝普通函数原型。', 'SymbolTable::insert('),
    ('记录完成', '先 declare_record，再 complete_record；成员类型和布局字段须合法。', 'SymbolTable::complete_record('),
    ('编号稳定', '编号为各表下标，只追加不排序；新增条目可能使旧指针失效，宜保存编号。', 'unsigned next_id('),
    ('偏移设置', 'set_symbol_offset 提供对象偏移接口；不可把接口存在当作所有对象已有偏移。', 'SymbolTable::set_symbol_offset('),
    ('内建函数', 'printf、scanf 可重复登记；先检查两个名字，冲突时不覆盖用户记录。', 'bool register_builtins('),
]


def build(driver):
    """构造六份表格，实际运行共用示例及语义边界例子。"""
    parser, _, _ = build_payload()
    by_name = {item['name']: item for item in parser['sheets']}
    books = {}
    def add(path, sheets):
        books[path] = {'schema': 1, 'header_row': 7, 'sheets': sheets}
    add('语法分析/语法表格/FIRST与FOLLOW表.xlsx', [by_name['FIRST与FOLLOW']])
    add('语法分析/语法表格/SELECT集表.xlsx', [by_name['SELECT集']])
    add('语法分析/语法表格/预测分析表.xlsx', [by_name[name] for name in
        ['预测分析表', '冲突与处理', '终结符', '语法约束']])

    sample = run_driver(driver, SAMPLE, 'run')
    if not sample['ok'] or not sample.get('equivalent'):
        raise ValueError('共用示例未通过编译及优化前后执行对照')
    for mode in ['baseline', 'optimized']:
        run = sample[mode]['run']
        if not run['ok'] or run['output'] != '16 A ok\n' or run['exit'] != 0:
            raise ValueError('示例执行结果不符合预期：' + mode)

    conversions, cases = [], []
    with tempfile.TemporaryDirectory(prefix='minic-tables-') as folder:
        case_path = Path(folder) / 'case.c'
        for target in ['char', 'int', 'float']:
            for source, value in [('char', "'A'"), ('int', '1'), ('float', '1.0')]:
                code = f'int main(){{{source} a={value};{target} b;b=a;return 0;}}'
                case_path.write_text(code, encoding='utf-8')
                actual = run_driver(driver, case_path)
                allowed = target == source or target == 'float' or (target == 'int' and source == 'char')
                codes = {item['code'] for item in actual['diagnostics']}
                if actual['ok'] != allowed or (not allowed and 'SEM_ASSIGN_TYPE' not in codes):
                    raise ValueError('隐式赋值规则已变化：' + code)
                conversions.append([target, source, '允许' if allowed else '拒绝',
                                    '同型' if target == source else 'ImplicitCast' if allowed else 'SEM_ASSIGN_TYPE', code])
        for title, code, expected in CASES:
            case_path.write_text(code, encoding='utf-8')
            actual = run_driver(driver, case_path)
            found = [item for item in actual['diagnostics'] if item['code'] == expected]
            if actual['ok'] or not found or any(item['phase'] not in ['语义'] for item in actual['diagnostics']):
                raise ValueError('语义边界已变化：' + title + ' / ' + str(actual['diagnostics']))
            cases.append([title, code, expected, found[0]['message']])

    rules = [[i, category, subject, rule, codes, ref(SEM, function)]
             for i, (category, subject, rule, function, codes) in enumerate(SEMANTIC_RULES, 1)]
    layouts = [
        ['char', '1', '1', '8 位有符号，范围 -128 至 127'],
        ['int', '4', '4', '32 位有符号'],
        ['float', '4', '4', '32 位浮点，运算按 float 精度舍入'],
        ['一维数组', '元素大小 × 长度', '元素对齐', '长度须确定且大于零；总大小不超过 16 MiB'],
        ['struct', '成员逐项对齐并补齐末尾', '成员最大对齐', '按记录身份取已完成的 size 与 alignment'],
        ['内部地址', '8', '8', '仅编译器数组传参与输入寻址使用，源码不可声明'],
        ['void / 函数', '不适用', '不适用', '不能作为普通存储对象布局'],
    ]
    add('语义分析/语义表格/语义分析表.xlsx', [
        sheet('语义规则', '当前源码语义检查规则', '按声明、初始化、表达式、函数、控制与输入输出分类。',
              ['编号', '类别', '对象', '规则', '相关诊断', '来源'], rules, [9, 18, 22, 85, 55, 43],
              ['只描述当前 MiniC 源码范围；后端保留的旧 AST 分支不代表源语言支持。']),
        sheet('隐式赋值转换', 'char / int / float 隐式赋值转换', '九种组合均由真实前端编译验证；目标对象不带 const。',
              ['目标类型', '来源类型', '结果', '转换或诊断', '验证源码'], conversions, [16, 16, 14, 28, 88],
              ['初始化、调用和返回也使用兼容性检查；char 参与一般算术会先提升到 int。']),
        sheet('类型与布局', '当前目标类型大小与对齐', '大小与对齐以字节为单位，采用项目固定模型，不依赖宿主 sizeof。',
              ['类型', '大小', '对齐', '说明'], layouts, [23, 44, 25, 85],
              ['来源：src/公共支持/internal.hpp 的 layout；结构体布局还见 analyzer.cpp 的 record_definition。',
               '对象大小最多 16 MiB，布局类型递归限制 128 层；旧扩展类型枚举不列为源语言支持。']),
        sheet('诊断验证示例', '真实语义拒绝示例', f'{len(cases)} 个最小源码；表内消息为本次编译器实际输出。',
              ['检查项', '源码', '预期且实际出现的代码', '实际消息'], cases, [24, 90, 36, 80]),
        sheet('示例类型标注', '共用示例的 AST 类型标注', '来自 workbench_driver 的语义结果；保留父编号、作用域和源码行。',
              ['节点编号', '父编号', '节点种类', '名字', '类型', '作用域编号', '源码行'],
              [[n['id'], n['parent'], n['kind'], n['name'] or '—', n['type'] or '—', n['scope'], n['line']] for n in sample['ast']],
              [13, 12, 25, 43, 35, 16, 12], ['父编号 -1 表示根；示例源码：实验设计/公共示例/表格示例.c。']),
        sheet('语义诊断索引', '语义代码来源索引', '源码字面量诊断代码及所在行；包含内部兼容分支，不作为可用语法列表。',
              ['代码', '源码所在行', '来源'], diagnostic_rows(SEM, 'SEM'), [36, 125, 43]),
    ])

    runtime_ops = set(re.findall(r'q\.op == "([^"]+)"', text('src/中间代码生成/interpreter.cpp')))
    if not {row[0] for row in OPERATIONS} <= runtime_ops:
        raise ValueError('四元式目录与解释器不一致')
    for mode in ['baseline', 'optimized']:
        known = {c['id'] for c in sample[mode]['constants']}
        symbols = {s['id'] for s in sample['symbols']}
        for q in sample[mode]['quads']:
            if q['op'] not in {row[0] for row in OPERATIONS}:
                raise ValueError('示例生成了未登记指令：' + q['op'])
            for key in ['arg1', 'arg2', 'result']:
                value = q[key]
                if value.startswith('%c') and int(value[2:]) not in known:
                    raise ValueError('示例常量引用无效')
                if value.startswith('%s') and int(value[2:]) not in symbols:
                    raise ValueError('示例符号引用无效')
    quad_sheet = lambda mode, name: sheet(name, name, '实际编译共用示例所得；每个函数的指令序号从 0 开始。',
        ['函数或分组', '序号', 'op', 'arg1', 'arg2', 'result'],
        [[q[k] for k in ['function', 'index', 'op', 'arg1', 'arg2', 'result']] for q in sample[mode]['quads']],
        [23, 10, 22, 24, 24, 24], ['%s 是符号编号，%c 是本程序常量池编号，%t 是组内临时量，L 是跳转标号；- 表示未使用。'])
    constant_rows = [[title, c['id'], c['type'], c['value'] if c['value'] != '' else '空字符串']
                     for mode, title in [('baseline', '未折叠'), ('optimized', '已折叠')]
                     for c in sample[mode]['constants']]
    control = [
        ['if / else', '条件求值；jz 假分支；真分支；jmp 结束；假分支；结束标号', '条件仅计算一次'],
        ['while', '条件标号；条件与 jz 结束；循环体；jmp 条件标号；结束', 'continue 到条件，break 到结束'],
        ['for', '初始化；条件标号；可选条件与 jz；体；更新标号与更新；jmp 条件；结束', 'continue 到更新，break 到结束'],
        ['&&', '先计算左边；jz 假；计算右边；jz 假；结果=1；跳过假分支；假结果=0', '右边按需求值，结果为 int'],
        ['||', '先计算左边；jnz 真；计算右边；jnz 真；结果=0；跳过真分支；真结果=1', '右边按需求值，结果为 int'],
        ['直接调用', '从左到右计算实参并保存快照；依次 arg；call；读取返回临时量', '函数内部递归调用使用独立帧'],
    ]
    add('中间代码生成/中间代码表格/中间代码生成表.xlsx', [
        sheet('四元式指令', '当前源码可生成的四元式', '字段顺序固定为 (op, arg1, arg2, result)；值输入可以引用常量或临时量。',
              ['op', 'arg1', 'arg2', 'result', '含义'], [[value.replace('左值输入', '左操作数').replace('右值输入', '右操作数') for value in row] for row in OPERATIONS], [22, 27, 27, 28, 70],
              ['操作码目录为人工依据生成器与解释器整理，已核对解释器识别及示例引用；不是全部内部兼容指令清单。',
               'call 的数量和 memberaddr 的偏移是元数据；源码不支持普通指针、间接调用或位运算。']),
        sheet('控制流翻译', '控制流与调用翻译规则', '依据 ir.cpp 整理；具体标号以示例四元式为准。',
              ['构造', '生成次序', '约束'], control, [20, 110, 62]),
        quad_sheet('baseline', '示例未折叠四元式'), quad_sheet('optimized', '示例已折叠四元式'),
        sheet('示例常量池', '共用示例的真实常量池', '未折叠与已折叠程序各拥有自己的池；编号不能跨程序混用。',
              ['版本', '编号', '类型', '解码值'], constant_rows, [16, 12, 40, 78]),
        sheet('常量池规则', '登记、去重与使用规则', '依据 constant_pool.cpp 与常量池说明整理；例子中的编号来自实际生成结果。',
              ['项目', '规则'], [
                  ['身份', '以类型和规范化值为键，源码原文与类型指针地址不参与去重。'],
                  ['限定符', 'TypeKind、unsigned、const 参与键；不能把不同类型同数值合并。'],
                  ['浮点', 'float 先按 float 舍入；精确表示比较，正零与负零分开，不作近似去重。'],
                  ['字符串', '保存完整解码字节串，包含内部零字节；值中不附末尾零，数组类型长度包含它。'],
                  ['类型快照', '使用独立只读类型快照；调用方修改原类型不会改变已有键。'],
                  ['永久编号', '从 0 开始按首次登记追加，不删除不排序；%c编号 只属于对应 IRProgram。'],
                  ['原文位置', '保留首次登记的 spelling 与 range；每次使用的位置在四元式 locations 中。'],
                  ['非法输入', '未解码值、类型不匹配、非有限浮点等拒绝入池，失败不增加条目。'],
              ], [24, 125]),
        sheet('IR数据结构', '四元式与程序容器字段', '从公共头文件提取；常量和临时量的类型信息须随程序一起保存。',
              ['结构', '字段声明', '含义', '来源'], field_rows(['Quadruple', 'Place', 'TemporaryEntry', 'IRFunction', 'IRProgram', 'ConstantEntry', 'ConstantPoolData']),
              [25, 75, 100, 43]),
        sheet('优化执行对照', '安全常量折叠的执行对照', '同一份源码分别关闭和开启 constant_folding 后实际执行。',
              ['版本', '四元式数量', '常量数量', '输出', '退出码', '执行步数'],
              [[title, len(sample[mode]['quads']), len(sample[mode]['constants']), sample[mode]['run']['output'],
                sample[mode]['run']['exit'], sample[mode]['run']['steps']]
               for mode, title in [('baseline', '未折叠'), ('optimized', '已折叠')]], [18, 19, 16, 40, 12, 17],
              ['输出均为 16 A ok 后换行；两侧实际运行成功、输出与退出码一致。仅比较本示例，不推断所有程序。']),
    ])
    apis = [
        ('enter_scope', '进入函数或块层', 'ScopeId'), ('exit_scope', '退出当前层，保留历史记录', 'bool'),
        ('insert', '登记形参或函数；局部对象内部也使用它', 'MaybeValue<SymbolId>'),
        ('declare_object', '登记对象并合并兼容全局声明', 'MaybeValue<SymbolId>'),
        ('lookup', '由当前层向外查普通名字', 'MaybeValue<SymbolId>'),
        ('lookup_current', '只查本层普通名字', 'MaybeValue<SymbolId>'),
        ('declare_record', '登记结构体标签', 'MaybeValue<RecordId>'),
        ('complete_record', '提交成员与布局', 'bool'),
        ('lookup_tag', '向外查标签', 'MaybeValue<RecordId>'),
        ('lookup_tag_current', '只查本层标签', 'MaybeValue<RecordId>'),
        ('find_member', '按记录编号查成员表下标', 'MaybeValue<size_t>'),
        ('symbol', '按编号读取符号', 'const SymbolEntry*'),
        ('record', '按编号读取记录', 'const StructEntry*'),
        ('set_symbol_offset', '设置对象的存储字节偏移', 'bool'),
    ]
    add('符号表管理/符号表格/符号表管理表.xlsx', [
        sheet('符号与作用域规则', '建表、查询与生命周期', '普通名字、结构体标签和常量池分开管理。',
              ['项目', '规则', '来源'], [[name, rule, ref(SYM, needle)] for name, rule, needle in SYMBOL_RULES], [23, 115, 43]),
        sheet('公共字段', '符号、成员、记录与作用域字段', '直接提取公共头文件声明及已有中文注释。',
              ['结构', '字段声明', '含义', '来源'], field_rows(['SymbolEntry', 'MemberEntry', 'StructEntry', 'ScopeEntry', 'SymbolTableData']),
              [25, 75, 100, 43]),
        sheet('建表查询接口', 'SymbolTable 操作接口', '失败时按接口返回空值、false 或 nullptr；诊断由收集器保存。',
              ['接口', '用途', '返回类型', '来源'],
              [[name, use, result, ref(SYM, 'SymbolTable::' + name + '(')] for name, use, result in apis], [28, 65, 32, 43]),
        sheet('示例符号表', '共用示例的真实普通符号表', '由语义分析实际建表；保留内建、全局、形参、局部与退出块后的符号。',
              ['符号编号', '名字', '类型', '作用域编号', '声明行'],
              [[s[k] for k in ['id', 'name', 'type', 'scope', 'line']] for s in sample['symbols']], [16, 25, 55, 18, 14],
              ['本表展示驱动公开的字段；未导出记录成员、父作用域与对象偏移，不能据此推断它们的具体值。',
               '两个 seed 位于不同作用域，符号编号不同；四元式使用 %s编号 绑定对象。内建行号不指向示例文件。']),
        sheet('符号诊断索引', '符号表诊断来源索引', '含重复声明、记录、成员、内建与无效操作等诊断。',
              ['代码', '源码所在行', '来源'], diagnostic_rows(SYM, 'SYM'), [36, 125, 43]),
    ])
    sources = [SEM, IR, SYM, INTERFACE, 'src/语义分析/type_rules.cpp',
               'src/中间代码生成/interpreter.cpp', 'src/中间代码生成/constant_pool.cpp',
               'src/公共支持/internal.hpp', 'tools/workbench_driver.cpp',
               'docs/公共约定/语言支持范围.md', 'tools/generate_parser_tables.py', str(SAMPLE.relative_to(ROOT)).replace('\\', '/')]
    snapshots = {name: hashlib.sha256(text(name).encode('utf-8')).hexdigest() for name in sources}
    return books, snapshots


def catalog(books, snapshots):
    """返回按五部分归类的交付清单及重生成说明。"""
    result = '# 五部分表格总览\n\n当前表格依据 MiniC 实现整理；不增加源语言功能。\n\n'
    result += '| 部分 | 文件 | 内容 |\n|---|---|---|\n'
    result += '| 词法分析 | [词法状态转换表](词法分析/词法表格/词法状态转换表.xlsx) | DFA、输入分类、规则、关键字、固定符号共 8 张表 |\n'
    result += '| 语法分析 | [文法表](语法分析/语法表格/文法表.xlsx)、[综合语法分析表](语法分析/语法表格/语法分析表.xlsx) | 产生式、运算符优先级、AST 孩子顺序、诊断和恢复等已有资料 |\n'
    for path, payload in books.items():
        result += f'| {path.split("/")[0]} | [{Path(path).stem}]({path}) | ' + '、'.join(s['name'] for s in payload['sheets']) + ' |\n'
    result += '\n语法集合针对带上下文条件的文法骨架；预测表的 3 个冲突格保留全部候选式。预测分析表附带冲突、终结符和约束，不能当作无冲突 LL(1) 解析器。\n\n'
    result += '语义、IR 与符号表共享[表格示例.c](公共示例/表格示例.c)。示例输出为 `16 A ok` 后换行，退出码 0。示例不是空白模板；符号编号、类型标注、常量池和两版四元式均从真实编译器读取，重新生成时会实际编译运行，并核对九种赋值转换与 14 个语义拒绝例子。\n\n'
    result += '设计规则为人工依据源码整理；字段和诊断索引自动提取，实例由驱动读取。来源快照用于提示资料需要更新，不证明规则完全覆盖源码行为。符号实例仅包含驱动公开字段，完整数据结构另列。\n\n'
    result += '## 重新生成与核对\n\n在根目录执行，先构建 workbench_driver。数据与预览放在被忽略的 build 目录；Excel 使用现有外部依赖。\n\n```powershell\n'
    result += 'python tools/generate_design_tables.py --data build/设计表格 --driver build/ninja/workbench_driver.exe\n'
    for index, path in enumerate(books, 1):
        result += f'node tools/export_frontend_tables.mjs build/设计表格/{index:02d}.json 实验设计/{path} build/设计表格/预览{index:02d} <node_modules目录>\n'
    result += 'python tools/generate_design_tables.py --check --driver build/ninja/workbench_driver.exe\n```\n\n'
    result += '核对入口逐单元格检查六份新增 Excel、示例执行和本文的来源快照；不修改现有词法与语法成品。\n\n## 来源快照\n\nUTF-8、统一 LF 换行后的 SHA-256。\n\n| 文件 | SHA-256 |\n|---|---|\n'
    for path, digest in snapshots.items():
        result += f'| [{path}](../{path}) | `{digest}` |\n'
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--data', type=Path, help='输出六份 Excel 的中间数据并更新总览')
    mode.add_argument('--check', action='store_true', help='核对真实编译结果及六份交付文件')
    parser.add_argument('--driver', type=Path, required=True, help='已构建的 workbench_driver')
    args = parser.parse_args()
    try:
        books, snapshots = build(args.driver.resolve())
        document = catalog(books, snapshots)
        if args.data:
            args.data.mkdir(parents=True, exist_ok=True)
            for index, (path, payload) in enumerate(books.items(), 1):
                (args.data / f'{index:02d}.json').write_text(json.dumps(payload, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
                print(f'{index:02d} {path} / {len(payload["sheets"])} 张工作表')
            (DESIGN / '表格总览.md').write_text(document, encoding='utf-8')
        else:
            for path, payload in books.items():
                print(f'{path}：{check_workbook(DESIGN / path, payload)} 个单元格一致')
            if (DESIGN / '表格总览.md').read_text(encoding='utf-8') != document:
                raise ValueError('表格总览或来源快照需要更新')
            print('六份设计表、九种转换、14 个诊断例子及示例优化执行对照通过')
    except (ValueError, OSError, subprocess.CalledProcessError, KeyError) as error:
        parser.exit(1, f'设计表格检查失败：{error}\n')


if __name__ == '__main__':
    main()
