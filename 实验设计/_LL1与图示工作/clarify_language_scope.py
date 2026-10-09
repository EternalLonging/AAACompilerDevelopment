from pathlib import Path
import json,shutil
root=Path(__file__).parent
out=root.parent/'LL1与图示'
def change(path,pairs):
    text=path.read_text('utf-8')
    for old,new in pairs:
        assert old in text,(path.name,old)
        text=text.replace(old,new)
    path.write_text(text,'utf-8')
change(root/'analyze_grammar.py',[
 ('# C89 文法候选及 LL(1) 检查','# 类 C89 项目语言文法候选及 LL(1) 检查'),
 ('版本：2026-10-09。完整 C89 构造范围，控制体使用花括号。不是未经证明的“严格 LL(1) 文法”。','版本：2026-10-09。项目设计类似 C89 的自定义 C 类语言，C89 仅作参考，功能与规则由项目决定。当前 BNF 是候选草案，控制体使用花括号。'),
 ('C89 块内先声明后语句；for 初始化为表达式。','当前草案采用块内先声明后语句、for 初始化为表达式；这些是草案规则，不是自动继承的标准要求。'),
 ('这是用户确认的 C89 花括号约束版本，','控制体花括号要求已由用户确认；'),
 ('因此不等价于无限制的全部 C89 程序集合。声明符、旧式函数定义、可变参数、位域、聚合初始化等仍保留。','声明符、旧式函数定义、可变参数、位域、聚合初始化等在当前候选中保留，尚不构成必须实现的功能清单。'),
 ('预处理（三字符组、续行、宏、条件编译、include）先于本 Token 语法；HASH/HASH_HASH 不进入普通语法。','预处理是独立阶段；三字符组、宏、条件编译等是参考构造，支持范围由项目另行定义。HASH/HASH_HASH 不进入当前普通语法。'),
 ('枚举不接受尾逗号（C89）；聚合初始化允许尾逗号。','当前草案枚举不接受尾逗号；聚合初始化允许尾逗号。'),
 ('本文 BNF 为本次整理的 C89 构造候选','本文 BNF 为以 C89 作参考整理的项目候选草案'),
 ('不能据此宣称原始 C89 Token 文法是严格 LL(1)。','项目语言类似 C89，具体规则以本草案和后续项目规范为准；整体 Token 适配流程不属于纯 LL(1)。'),
 ('不能把 a+b=c 当作标准 C89 合法语法。','当前草案不接受 a+b=c；是否调整此约束须修改文法并重算集合。'),
 ('控制语句位置仍按 C89 语义验证。','控制语句位置按项目语义规范验证，C89 仅作为参考；现有候选构造不自动成为全部实现要求。'),
 ('C89 块内先声明后语句，for 初始化不允许 C99 风格声明。','当前草案块内先声明后语句，for 初始化不接受声明；这些具体规则可按项目需求调整。')
])
change(root/'build_diagrams.mjs',[
 ('C89 递归下降语法分析总流程','类 C89 项目语言语法分析总流程'),
 ('交给后续 C89 语义检查','交给项目语义检查'),
 ('项目规划为 C++17 手写递归下降；此图是设计流程，不表示正式分析器已实现。','项目语言类似 C89，具体规则由项目定义；规划为 C++17 手写递归下降，此图为设计流程。')
])
change(root/'build_pdf.py',[
 ('import json,re,xml.etree.ElementTree as ET','import json,re,sys,xml.etree.ElementTree as ET'),
 ('C89 上下文 LL(1) 语法分析流程','类 C89 项目语言的 LL(1) 语法分析流程')
])
# Re-export only the changed syntax PDF in this correction.
p=root/'build_pdf.py';text=p.read_text('utf-8');start=text.index('statepath=out/');end=text.index("flowpath=out/")
text=text[:start]+"if '--flow-only' not in sys.argv:\n"+''.join('    '+line if line.strip() else line for line in text[start:end].splitlines(keepends=True))+text[end:]
text=text.replace("'state_pdf_pages':len(PdfReader(statepath).pages)","'state_pdf_pages':len(PdfReader(out/'词法状态转换图.pdf').pages)")
text=text.replace("'state_diagram_pages':pages","'state_diagram_pages':pages if '--flow-only' not in sys.argv else json.loads((root/'pdf_validation.json').read_text('utf-8'))['state_diagram_pages']")
p.write_text(text,'utf-8')
change(out/'交付说明.md',[
 ('# C89 集合与图示交付说明','# 类 C89 项目语言集合与图示交付说明'),
 ('覆盖 C89 的声明、','项目以 C89 为语法参考。当前候选草案包含声明、'),
 ('及全部语句构造。预处理','及语句构造；这些候选功能不自动成为项目必须实现的范围。预处理'),
 ('因此这是具有明确花括号限制的 C89 构造设计，不能宣称接受所有不加限制的标准 C89 源程序。块内先声明后语句，for 初始化限表达式；不引入 C99 构造。','项目目标是类似 C89 的自定义 C 类语言，具体文法与语义由项目决定，不以完全遵循 C89 标准为验收目标。当前草案采用块内先声明后语句、for 初始化限表达式，尚未引入 C99 构造；后续可按项目需求调整。'),
 ('整体 C89 识别还必须执行：','实现当前草案还必须执行：'),
 ('后续 C89 语义检查','后续项目语义检查（C89 作为参考）'),
 ('验证脚本不能代替完整 C89 编译器。','验证脚本不能代替正式项目编译器。文件名中保留 C89 字样仅用于保持既有链接，表示参考来源。')
])
shutil.copy2(out/'C89_LL1集合与预测分析表.xlsx',root/'scope_edit_before.xlsx')
print('Project language scope clarified; grammar alternatives preserved.')
