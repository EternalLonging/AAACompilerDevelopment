# 词法分析正则表达式：最终规则 v1.0

本规则按项目最终的 **C89** 词法范围整理，包含完整数字形式和转义。保留项目要求的 // 单行注释扩展；该扩展本身不属于严格 C89。

规则文件：[lexer/patterns.json](../lexer/patterns.json)。Token 名称与 [interface.hpp](../include/minic/interface.hpp) 一致。验证工具：[tools/check_lexer_patterns.py](../tools/check_lexer_patterns.py)。

本次完成正则规范及验证，尚未生成 NFA/DFA 或实现 C++ 扫描器。词法接受某种拼写，不代表语法、语义和解释器已经实现对应功能。

## 1. 规则文件与正则语法

| 字段 | 含义 |
|---|---|
| definitions | 正则宏；{DIGIT} 是定义引用，不是重复次数 |
| keywords | 完整标识符原文到 KW_* 种别的映射，共 32 项 |
| rules | 按优先顺序排列的普通词法规则，共 64 条 |
| rules[].name / regex | 规则唯一名字 / 正则模式 |
| rules[].action | token：输出单词；skip：跳过但更新位置；error：报错且不输出单词 |
| rules[].token / code | 输出的 TokenType / 错误诊断代码，仅相应动作使用 |
| rules[].literal / description | 固定符号源码拼写 / 中文说明 |
| preprocessing | 预处理顺序及 2 条头文件上下文规则 |
| eof_token | 输入结束时额外生成的 END_OF_FILE |

正则子集使用连接、选择 |、分组 ()、字符类 []、否定字符类 [^]、闭包 *、正闭包 +、可选 ?、宏与转义。支持 \n、\r、\t、\v、\f、\xHH 以及正则元字符的字面转义。

不使用环视、反向引用、懒惰量词或捕获组业务逻辑，全部都是可转换为 NFA/DFA 的正规语言。正则中的 \xHH 表示一个匹配字节；C 源码中的 \x41 则是多个待识别字节，两者不能混淆。

JSON 对反斜杠另外转义了一层：JSON 的 "\\." 解码后才是正则 \.。下文的正则展示的是 **JSON 解码后的内容**。

扫描字母表为 256 个字节值。标识符使用 ASCII，UTF-8 字节可出现在字符串、字符常量和注释中；非 ASCII 标识符不属于本 C89 规则。源码范围按字节记录，展示层负责视觉列换算。

## 2. 基础宏

    DIGIT         = [0-9]
    LETTER        = [A-Za-z_]
    ID_CONTINUE   = [A-Za-z0-9_]
    HEX_DIGIT     = [0-9A-Fa-f]
    EXPONENT      = [eE][+-]?{DIGIT}+
    INT_SUFFIX    = ([uU][lL]?|[lL][uU]?)?
    DECIMAL_INT   = [1-9]{DIGIT}*
    OCTAL_INT     = 0[0-7]*
    HEX_INT       = 0[xX]{HEX_DIGIT}+
    SIMPLE_ESCAPE = \\[abfnrtv\\'"?]
    OCTAL_ESCAPE  = \\[0-7][0-7]?[0-7]?
    HEX_ESCAPE    = \\x{HEX_DIGIT}+
    ESCAPE        = {SIMPLE_ESCAPE}|{OCTAL_ESCAPE}|{HEX_ESCAPE}
    STRING_CHAR   = [^"\\\r\n]
    CHAR_CHAR     = [^'\\\r\n]

LETTER 包含下划线，ID_CONTINUE 允许数字。EXPONENT 的数字部分至少一位。

## 3. 标识符与关键字

    ID = {LETTER}{ID_CONTINUE}*

合法名字包括 r、main、printf、scanf、_count、value2。识别完整名字后再查下面 32 个关键字：

    auto      break     case      char      const     continue
    default   do        double    else      enum      extern
    float     for       goto      if        int       long
    register  return    short     signed    sizeof    static
    struct    switch    typedef   union     unsigned  void
    volatile  while

例如 int 改为 KW_INT，int32 仍是 ID。main、printf、scanf、include 均不是关键字。typedef 类型别名仍是 ID，消歧由解析环境处理，正则不猜测。

预处理指令/宏处理时名字首先属于预处理标识符；宏处理完成后交给普通 C 语法的单词才做关键字分类，不能让 KW_* 分类阻止宏上下文处理。

## 4. 整数常量

    INT_LITERAL = ({HEX_INT}|{OCTAL_INT}|{DECIMAL_INT}){INT_SUFFIX}

| 分类 | 示例 | 约定 |
|---|---|---|
| 十进制 | 1、42、123 | 非零数字开头 |
| 八进制 | 0、00、077 | 0 开头，只允许 0–7 |
| 十六进制 | 0xff、0XAB | 0x/0X 开头，至少一位十六进制数字 |
| 无符号后缀 | 123u、123U | u/U |
| long 后缀 | 123l、123L | l/L |
| 组合后缀 | 123UL、123LU、123uL、123lU | u/U 与 l/L 各最多一个，顺序不限 |

**012 是八进制，值为十进制 10**，不再沿用早期“前导零也按十进制”的简化规则。08、0x、1UU、1LL、0b101 为非法形式；C89 不包含 long long 后缀或二进制常量。

词法保留完整原文；进制转换、整数候选类型选择和数值越界由语义处理，正则不限制数字位数。-12 是 MINUS 与 INT_LITERAL 两个单词，符号不是整数模式的一部分。

## 5. 浮点常量

    FLOAT_LITERAL = (({DIGIT}+\.{DIGIT}*|\.{DIGIT}+){EXPONENT}?|{DIGIT}+{EXPONENT})[fFlL]?

| 形式 | 示例 |
|---|---|
| 普通小数 | 3.14159、0.25 |
| 小数点右边省略数字 | 1.、10. |
| 小数点左边省略数字 | .5、.125 |
| 仅有指数 | 1e3、2E-4 |
| 小数加指数 | 1.e+2、.5e-2、3.14e0 |
| 精度后缀 | 1.0f、1e2F、2.5L |

必须有小数点或指数；123 不是浮点，1f 也不合法。最终 C89 规则下，无后缀为 double，f/F 为 float，l/L 为 long double，均使用 FLOAT_LITERAL，由语义读取后缀确定类型。

0x1.fp3 是 C99 十六进制浮点，不在本 C89 规则内。README 中 C89/C99 验收口径尚有冲突，本最终词法规范明确选择 C89；若后续确认 C99，需明确追加规则。

## 6. 字符与字符串

    CHAR_LITERAL   = L?'({CHAR_CHAR}|{ESCAPE})+'
    STRING_LITERAL = L?"({STRING_CHAR}|{ESCAPE})*"

字符常量非空，字符串可为空。L 前缀表示宽字符/宽字符串。

    'a'          普通字符常量
    '\n'         简单转义
    '\123'       最多三位八进制转义
    '\x41'       至少一位、可多位的十六进制转义
    L'a'         宽字符常量
    ""           空字符串
    "c = %f\n"   普通字符串，% 和 f 都是字符串内容
    "\"quote\""  引号转义
    L"wide"      宽字符串

简单转义包括 \a、\b、\f、\n、\r、\t、\v、\\、\'、\"、\?。\0 属于八进制转义。

\1234 分成转义 \123 与普通字符 4；\x4142 中连续的十六进制数字都属于同一转义。解码后是否超出目标字符范围由语义/目标模型判断。\8、\q、缺少数字的 \x、C99 的 \u0041 属于本规则非法转义。

C89 允许 'ab' 这样的多字符常量，其值由实现定义，因此最终规则接受非空的多项内容。编码、窄/宽值、常量类型和多字符值由语义与目标模型约定；不能认为一个 UTF-8 字符必然占一个字节。

词法保留引号、L 前缀和转义原文，由语义解码。相邻字符串分别输出 STRING_LITERAL，后续再拼接。引号内的 // 与 /* 不成为注释。

## 7. 运算符与分隔符

所有固定符号逐项定义于 patterns.json，与 TokenType 全覆盖对应：

    算术及递增递减： +  -  *  /  %  ++  --
    赋值：           =  +=  -=  *=  /=  %=  &=  |=  ^=  <<=  >>=
    关系与相等：     <  <=  >  >=  ==  !=
    逻辑：           &&  ||  !
    位操作及取地址： &  |  ^  ~  <<  >>
    成员与条件：     .  ->  ?  :
    分隔符：         (  )  {  }  [  ]  ;  ,
    省略号：         ...
    预处理符号：     #  ##

正则元字符必须转义，例如：

    PLUS     = \+
    STAR     = \*
    LPAREN   = \(
    LBRACKET = \[
    DOT      = \.
    OR       = \|\|
    ELLIPSIS = \.\.\.

& 的 Token 种别为 AMP，它表示取地址还是按位与由语法决定；* 同理可表示乘法、解引用或指针声明。<<= 必须整体输出 SHIFT_LEFT_ASSIGN，... 不能拆成三个 DOT，由最长匹配保证。

## 8. 空白与注释

    WHITESPACE    = [ \t\v\f]+
    NEWLINE       = \r\n|\r|\n
    LINE_COMMENT  = //[^\r\n]*
    BLOCK_COMMENT = /\*([^*]|\*+[^*/])*\*+/

四类动作均为 skip，不输出 Token，但更新位置。CRLF 是一次换行、两个源字节；Tab 按项目字节列规则算一列。

块注释接受空注释、多星号和跨行内容，终止于第一个 */，无需懒惰量词。不会将两个独立注释之间的代码一起吞掉。C 注释不嵌套，例如：

    /* outer /* inner */ x

这里第一个 */ 已结束注释，x 在注释外。// 是本项目保留的扩展。完整预处理输出文本时，注释应替换为空白，不能把 int/**/x 拼成 intx；普通扫描跳过注释也必须保留单词区别。

## 9. 错误规则

| 规则 | 诊断代码 | 对象 |
|---|---|---|
| INVALID_NUMBER | LEX_INVALID_NUMBER | 非法进制、残缺指数、错误后缀等 |
| INVALID_STRING | LEX_INVALID_STRING | 已闭合但含非法转义的字符串 |
| INVALID_CHAR_LITERAL | LEX_INVALID_CHAR_LITERAL | 已闭合的空字符常量或非法转义 |
| UNTERMINATED_STRING | LEX_UNTERMINATED_STRING | 换行/EOF 前没有闭合的字符串 |
| UNTERMINATED_CHAR | LEX_UNTERMINATED_CHAR | 换行/EOF 前没有闭合的字符常量 |
| UNTERMINATED_COMMENT | LEX_UNTERMINATED_COMMENT | EOF 前没有 */ 的块注释 |
| INVALID_BYTE | LEX_INVALID_CHAR | 其他无法匹配的字节，例如 @ |

    INVALID_NUMBER       = ({DIGIT}|\.{DIGIT})([eEpP][+-]|[A-Za-z0-9_.])*
    INVALID_STRING       = L?"({STRING_CHAR}|\\[^\r\n])*"
    INVALID_CHAR_LITERAL = L?'({CHAR_CHAR}|\\[^\r\n])*'
    UNTERMINATED_STRING  = L?"({STRING_CHAR}|\\[^\r\n])*(\\)?
    UNTERMINATED_CHAR    = L?'({CHAR_CHAR}|\\[^\r\n])*(\\)?
    UNTERMINATED_COMMENT = /\*([^*]|\*+[^*/])*\**
    INVALID_BYTE         = [\x00-\xFF]

INVALID_NUMBER 是宽松数字候选串，不是合法数字定义。合法数字与它匹配同长时，前面的 INT_LITERAL/FLOAT_LITERAL 优先；候选串更长时整体报错。12abc 不拆成 12 和 abc，1e+ 不拆成 1、e、+。

0x1e+2 构成一个宽松预处理数字候选串，最终不是合法 C89 数字，整体报错；表达十六进制数相加应写 0x1e + 2。错误规则额外吞入 p/P 后的指数符号是为了完整报告未支持的十六进制浮点，不表示支持该形式。

合法字面量规则在错误规则前，合法同长匹配优先。未闭合引号消费到换行之前，保留换行以更新位置。块注释以 /* 开始却未闭合时整体消费到 EOF，不能回退成 SLASH、STAR 等单词。

错误动作只产生诊断，不输出伪造 Token；按恢复策略继续。本阶段有错误就不进入语法分析，扫描结束仍追加 EOF。

## 10. 最长匹配与自动机生成

当前位置选择规则：

1. 选择消费字节数最多的接受结果。
2. 长度相同按 patterns.json 的顺序，前面的优先。

**最长匹配先于规则优先级。** 不能用“组合正则的第一个匹配分支”冒充最长匹配。

生成器先展开宏，为各规则构造 NFA；终态绑定规则编号、action、token/code。新起点通过空转移连接各规则 NFA，子集构造时为接受态保留优先级最高的终态元数据。

最小化时输出动作/Token/诊断代码不同的接受态不能随意合并。ID 和 INT_LITERAL 输出不同，STRING_LITERAL 与 INVALID_STRING 动作不同，不能仅因同为接受态就合并。

扫描器持续推进，记录最后接受状态及位置；无法继续时回到最后接受位置并执行动作。全部普通规则至少消费一字节，EOF 不使用能接受空串的正则，扫描结束后单独追加 END_OF_FILE。

## 11. 预处理与头文件上下文

正确的最终处理顺序包括：

1. 规范源字符和 C89 三字符组，维护原始位置映射。
2. 删除反斜杠紧接换行的续行，维护映射。
3. 处理注释、预处理单词、指令、头文件、宏及条件编译。
4. 普通 C 单词进行关键字和合法数字分类，再交给语法分析。

三字符组：

    ??= -> #    ??( -> [    ??/ -> \    ??) -> ]
    ??' -> ^    ??< -> {    ??! -> |    ??> -> }    ??- -> ~

续行删除早于注释和字符串识别。跨行名字可连接为一个标识符，单行注释也可因续行延伸，不能先按物理行识别再补救。

仅在 #include 头文件上下文使用：

    HEADER_NAME_ANGLE  = <[^>\r\n]+>
    HEADER_NAME_QUOTED = "[^"\r\n]+"

这两条位于 preprocessing.header_rules，**不加入普通单词 DFA**，否则 a < b > c 会被误认为头文件。头文件名不是 C 字符串，反斜杠不按字符串转义解码；必须闭合且内容非空。include 参数经宏展开后，也须按正确上下文重新识别。

# 和 ## 已有固定符号定义，但文件搜索、宏参数、条件编译不是正则的职责。宏粘贴后必须校验所得拼写构成一个完整预处理单词，不能拼接后直接当作合法 C Token。

扫描相应阶段处理过的文本，Token.range/Diagnostic.range 仍映射回用户原文件。规则验证工具不实现或宣称验证了这些预处理步骤。

## 12. 验证结果

共 17 组检查通过，覆盖：正则编译及非空匹配、TokenType 全覆盖、32 关键字边界、全部固定符号、整数进制/后缀、浮点指数/精度、普通/宽字面量、转义、非法数字、未闭合引号/注释、最长匹配、换行和字节位置、头文件上下文。

圆周长 circle.c 得到 **35 个 Token（含 EOF）**，scanf 的 & 识别为 AMP。

    python tools/check_lexer_patterns.py

验证工具使用 Python 字节正则逐规则比较匹配长度，检查规范行为；正式 C++ 词法模块仍通过自研 NFA/DFA 生成器实现。
