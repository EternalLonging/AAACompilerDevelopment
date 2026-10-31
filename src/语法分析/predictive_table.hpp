// 自动生成：python tools/generate_parser_tables.py --runtime；请修改生成器中的 GRAMMAR。
#pragma once
#include "minic/interface.hpp"

namespace minic { namespace predictive {
inline constexpr int terminal_count = 43; // 终结符数量，编号与 TokenType 一致。
enum Nonterminal {
    程序 = 43,
    外部声明 = 44,
    外部声明尾 = 45,
    函数尾 = 46,
    局部声明 = 47,
    结构声明尾 = 48,
    定义后 = 49,
    对象尾 = 50,
    对象续 = 51,
    初值可选 = 52,
    标量类型 = 53,
    成员序列 = 54,
    成员声明 = 55,
    成员类型 = 56,
    成员续 = 57,
    数组可选 = 58,
    界限可选 = 59,
    形参表可选 = 60,
    形参续 = 61,
    形参 = 62,
    块 = 63,
    局部声明序列 = 64,
    语句序列 = 65,
    语句 = 66,
    否则可选 = 67,
    表达式可选 = 68,
    表达式 = 69,
    赋值续 = 70,
    逻辑或式 = 71,
    逻辑或续 = 72,
    逻辑与式 = 73,
    逻辑与续 = 74,
    相等式 = 75,
    相等续 = 76,
    关系式 = 77,
    关系续 = 78,
    加法式 = 79,
    加法续 = 80,
    乘法式 = 81,
    乘法续 = 82,
    一元式 = 83,
    后缀式 = 84,
    主式 = 85,
    字符串 = 86,
    字符串续 = 87,
    表达后缀序列 = 88,
    表达后缀 = 89,
    实参可选 = 90,
    实参续 = 91,
};
struct Production {
    int length; // 右部符号个数，0 表示空产生式。
    int rhs[9]; // 右部符号编号，保持文法顺序。
};
inline constexpr Production productions[] = {
    {2, {44, 43}}, // P001: 程序 -> 外部声明 程序
    {0, {0}}, // P002: 程序 -> ε
    {3, {53, 1, 45}}, // P003: 外部声明 -> 标量类型 ID 外部声明尾
    {3, {15, 1, 46}}, // P004: 外部声明 -> KW_VOID ID 函数尾
    {3, {9, 1, 48}}, // P005: 外部声明 -> KW_STRUCT ID 结构声明尾
    {1, {46}}, // P006: 外部声明尾 -> 函数尾
    {1, {50}}, // P007: 外部声明尾 -> 对象尾
    {4, {31, 60, 32, 63}}, // P008: 函数尾 -> LPAREN 形参表可选 RPAREN 块
    {3, {53, 1, 50}}, // P009: 局部声明 -> 标量类型 ID 对象尾
    {3, {9, 1, 48}}, // P010: 局部声明 -> KW_STRUCT ID 结构声明尾
    {5, {33, 55, 54, 34, 49}}, // P011: 结构声明尾 -> LBRACE 成员声明 成员序列 RBRACE 定义后
    {2, {1, 50}}, // P012: 结构声明尾 -> ID 对象尾
    {2, {1, 50}}, // P013: 定义后 -> ID 对象尾
    {1, {37}}, // P014: 定义后 -> SEMI
    {4, {58, 52, 51, 37}}, // P015: 对象尾 -> 数组可选 初值可选 对象续 SEMI
    {5, {38, 1, 58, 52, 51}}, // P016: 对象续 -> COMMA ID 数组可选 初值可选 对象续
    {0, {0}}, // P017: 对象续 -> ε
    {2, {20, 69}}, // P018: 初值可选 -> ASSIGN 表达式
    {0, {0}}, // P019: 初值可选 -> ε
    {1, {8}}, // P020: 标量类型 -> KW_CHAR
    {1, {6}}, // P021: 标量类型 -> KW_INT
    {1, {7}}, // P022: 标量类型 -> KW_FLOAT
    {2, {55, 54}}, // P023: 成员序列 -> 成员声明 成员序列
    {0, {0}}, // P024: 成员序列 -> ε
    {5, {56, 1, 58, 57, 37}}, // P025: 成员声明 -> 成员类型 ID 数组可选 成员续 SEMI
    {1, {53}}, // P026: 成员类型 -> 标量类型
    {2, {9, 1}}, // P027: 成员类型 -> KW_STRUCT ID
    {4, {38, 1, 58, 57}}, // P028: 成员续 -> COMMA ID 数组可选 成员续
    {0, {0}}, // P029: 成员续 -> ε
    {3, {35, 59, 36}}, // P030: 数组可选 -> LBRACKET 界限可选 RBRACKET
    {0, {0}}, // P031: 数组可选 -> ε
    {1, {2}}, // P032: 界限可选 -> INT_LITERAL
    {0, {0}}, // P033: 界限可选 -> ε
    {1, {15}}, // P034: 形参表可选 -> KW_VOID
    {2, {62, 61}}, // P035: 形参表可选 -> 形参 形参续
    {0, {0}}, // P036: 形参表可选 -> ε
    {3, {38, 62, 61}}, // P037: 形参续 -> COMMA 形参 形参续
    {0, {0}}, // P038: 形参续 -> ε
    {3, {53, 1, 58}}, // P039: 形参 -> 标量类型 ID 数组可选
    {6, {9, 1, 1, 35, 59, 36}}, // P040: 形参 -> KW_STRUCT ID ID LBRACKET 界限可选 RBRACKET
    {4, {33, 64, 65, 34}}, // P041: 块 -> LBRACE 局部声明序列 语句序列 RBRACE
    {2, {47, 64}}, // P042: 局部声明序列 -> 局部声明 局部声明序列
    {0, {0}}, // P043: 局部声明序列 -> ε
    {2, {66, 65}}, // P044: 语句序列 -> 语句 语句序列
    {0, {0}}, // P045: 语句序列 -> ε
    {1, {63}}, // P046: 语句 -> 块
    {1, {37}}, // P047: 语句 -> SEMI
    {6, {11, 31, 69, 32, 63, 67}}, // P048: 语句 -> KW_IF LPAREN 表达式 RPAREN 块 否则可选
    {5, {13, 31, 69, 32, 63}}, // P049: 语句 -> KW_WHILE LPAREN 表达式 RPAREN 块
    {9, {14, 31, 68, 37, 68, 37, 68, 32, 63}}, // P050: 语句 -> KW_FOR LPAREN 表达式可选 SEMI 表达式可选 SEMI 表达式可选 RPAREN 块
    {3, {10, 68, 37}}, // P051: 语句 -> KW_RETURN 表达式可选 SEMI
    {2, {40, 37}}, // P052: 语句 -> KW_BREAK SEMI
    {2, {41, 37}}, // P053: 语句 -> KW_CONTINUE SEMI
    {2, {69, 37}}, // P054: 语句 -> 表达式 SEMI
    {2, {12, 63}}, // P055: 否则可选 -> KW_ELSE 块
    {0, {0}}, // P056: 否则可选 -> ε
    {1, {69}}, // P057: 表达式可选 -> 表达式
    {0, {0}}, // P058: 表达式可选 -> ε
    {2, {71, 70}}, // P059: 表达式 -> 逻辑或式 赋值续
    {2, {20, 69}}, // P060: 赋值续 -> ASSIGN 表达式
    {0, {0}}, // P061: 赋值续 -> ε
    {2, {73, 72}}, // P062: 逻辑或式 -> 逻辑与式 逻辑或续
    {3, {28, 73, 72}}, // P063: 逻辑或续 -> OR 逻辑与式 逻辑或续
    {0, {0}}, // P064: 逻辑或续 -> ε
    {2, {75, 74}}, // P065: 逻辑与式 -> 相等式 逻辑与续
    {3, {27, 75, 74}}, // P066: 逻辑与续 -> AND 相等式 逻辑与续
    {0, {0}}, // P067: 逻辑与续 -> ε
    {2, {77, 76}}, // P068: 相等式 -> 关系式 相等续
    {3, {25, 77, 76}}, // P069: 相等续 -> EQ 关系式 相等续
    {3, {26, 77, 76}}, // P070: 相等续 -> NE 关系式 相等续
    {0, {0}}, // P071: 相等续 -> ε
    {2, {79, 78}}, // P072: 关系式 -> 加法式 关系续
    {3, {21, 79, 78}}, // P073: 关系续 -> LT 加法式 关系续
    {3, {22, 79, 78}}, // P074: 关系续 -> LE 加法式 关系续
    {3, {23, 79, 78}}, // P075: 关系续 -> GT 加法式 关系续
    {3, {24, 79, 78}}, // P076: 关系续 -> GE 加法式 关系续
    {0, {0}}, // P077: 关系续 -> ε
    {2, {81, 80}}, // P078: 加法式 -> 乘法式 加法续
    {3, {16, 81, 80}}, // P079: 加法续 -> PLUS 乘法式 加法续
    {3, {17, 81, 80}}, // P080: 加法续 -> MINUS 乘法式 加法续
    {0, {0}}, // P081: 加法续 -> ε
    {2, {83, 82}}, // P082: 乘法式 -> 一元式 乘法续
    {3, {18, 83, 82}}, // P083: 乘法续 -> STAR 一元式 乘法续
    {3, {19, 83, 82}}, // P084: 乘法续 -> SLASH 一元式 乘法续
    {3, {42, 83, 82}}, // P085: 乘法续 -> PERCENT 一元式 乘法续
    {0, {0}}, // P086: 乘法续 -> ε
    {2, {16, 83}}, // P087: 一元式 -> PLUS 一元式
    {2, {17, 83}}, // P088: 一元式 -> MINUS 一元式
    {2, {29, 83}}, // P089: 一元式 -> NOT 一元式
    {2, {30, 83}}, // P090: 一元式 -> AMP 一元式
    {1, {84}}, // P091: 一元式 -> 后缀式
    {2, {85, 88}}, // P092: 后缀式 -> 主式 表达后缀序列
    {1, {1}}, // P093: 主式 -> ID
    {1, {2}}, // P094: 主式 -> INT_LITERAL
    {1, {3}}, // P095: 主式 -> FLOAT_LITERAL
    {1, {4}}, // P096: 主式 -> CHAR_LITERAL
    {1, {86}}, // P097: 主式 -> 字符串
    {3, {31, 69, 32}}, // P098: 主式 -> LPAREN 表达式 RPAREN
    {2, {5, 87}}, // P099: 字符串 -> STRING_LITERAL 字符串续
    {2, {5, 87}}, // P100: 字符串续 -> STRING_LITERAL 字符串续
    {0, {0}}, // P101: 字符串续 -> ε
    {2, {89, 88}}, // P102: 表达后缀序列 -> 表达后缀 表达后缀序列
    {0, {0}}, // P103: 表达后缀序列 -> ε
    {3, {31, 90, 32}}, // P104: 表达后缀 -> LPAREN 实参可选 RPAREN
    {3, {35, 69, 36}}, // P105: 表达后缀 -> LBRACKET 表达式 RBRACKET
    {2, {39, 1}}, // P106: 表达后缀 -> DOT ID
    {2, {69, 91}}, // P107: 实参可选 -> 表达式 实参续
    {0, {0}}, // P108: 实参可选 -> ε
    {3, {38, 69, 91}}, // P109: 实参续 -> COMMA 表达式 实参续
    {0, {0}}, // P110: 实参续 -> ε
};
// 行为非终结符，列为 TokenType；值为产生式下标，-1 表示语法错误。
inline constexpr int table[][43] = {
    {1, -1, -1, -1, -1, -1, 0, 0, 0, 0, -1, -1, -1, -1, -1, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, // 程序
    {-1, -1, -1, -1, -1, -1, 2, 2, 2, 4, -1, -1, -1, -1, -1, 3, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, // 外部声明
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 6, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 5, -1, -1, -1, 6, -1, 6, 6, -1, -1, -1, -1}, // 外部声明尾
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 7, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, // 函数尾
    {-1, -1, -1, -1, -1, -1, 8, 8, 8, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, // 局部声明
    {-1, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1}, // 结构声明尾
    {-1, 12, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 13, -1, -1, -1, -1, -1}, // 定义后
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 14, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 14, -1, 14, 14, -1, -1, -1, -1}, // 对象尾
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 16, 15, -1, -1, -1, -1}, // 对象续
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 17, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 18, 18, -1, -1, -1, -1}, // 初值可选
    {-1, -1, -1, -1, -1, -1, 20, 21, 19, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, // 标量类型
    {-1, -1, -1, -1, -1, -1, 22, 22, 22, 22, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 23, -1, -1, -1, -1, -1, -1, -1, -1}, // 成员序列
    {-1, -1, -1, -1, -1, -1, 24, 24, 24, 24, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, // 成员声明
    {-1, -1, -1, -1, -1, -1, 25, 25, 25, 26, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, // 成员类型
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 28, 27, -1, -1, -1, -1}, // 成员续
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 30, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 30, -1, -1, 29, -1, 30, 30, -1, -1, -1, -1}, // 数组可选
    {-1, -1, 31, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 32, -1, -1, -1, -1, -1, -1}, // 界限可选
    {-1, -1, -1, -1, -1, -1, 34, 34, 34, 34, -1, -1, -1, -1, -1, 33, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 35, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, // 形参表可选
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 37, -1, -1, -1, -1, -1, 36, -1, -1, -1, -1}, // 形参续
    {-1, -1, -1, -1, -1, -1, 38, 38, 38, 39, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, // 形参
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 40, -1, -1, -1, -1, -1, -1, -1, -1, -1}, // 块
    {-1, 42, 42, 42, 42, 42, 41, 41, 41, 41, 42, 42, -1, 42, 42, -1, 42, 42, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 42, 42, 42, -1, 42, 42, -1, -1, 42, -1, -1, 42, 42, -1}, // 局部声明序列
    {-1, 43, 43, 43, 43, 43, -1, -1, -1, -1, 43, 43, -1, 43, 43, -1, 43, 43, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 43, 43, 43, -1, 43, 44, -1, -1, 43, -1, -1, 43, 43, -1}, // 语句序列
    {-1, 53, 53, 53, 53, 53, -1, -1, -1, -1, 50, 47, -1, 48, 49, -1, 53, 53, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 53, 53, 53, -1, 45, -1, -1, -1, 46, -1, -1, 51, 52, -1}, // 语句
    {-1, 55, 55, 55, 55, 55, -1, -1, -1, -1, 55, 55, 54, 55, 55, -1, 55, 55, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 55, 55, 55, -1, 55, 55, -1, -1, 55, -1, -1, 55, 55, -1}, // 否则可选
    {-1, 56, 56, 56, 56, 56, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 56, 56, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 56, 56, 56, 57, -1, -1, -1, -1, 57, -1, -1, -1, -1, -1}, // 表达式可选
    {-1, 58, 58, 58, 58, 58, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 58, 58, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 58, 58, 58, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, // 表达式
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 59, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 60, -1, -1, -1, 60, 60, 60, -1, -1, -1, -1}, // 赋值续
    {-1, 61, 61, 61, 61, 61, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 61, 61, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 61, 61, 61, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, // 逻辑或式
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 63, -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63, -1, -1, -1, 63, 63, 63, -1, -1, -1, -1}, // 逻辑或续
    {-1, 64, 64, 64, 64, 64, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 64, 64, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 64, 64, 64, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, // 逻辑与式
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 66, -1, -1, -1, -1, -1, -1, 65, 66, -1, -1, -1, 66, -1, -1, -1, 66, 66, 66, -1, -1, -1, -1}, // 逻辑与续
    {-1, 67, 67, 67, 67, 67, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 67, 67, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 67, 67, 67, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, // 相等式
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 70, -1, -1, -1, -1, 68, 69, 70, 70, -1, -1, -1, 70, -1, -1, -1, 70, 70, 70, -1, -1, -1, -1}, // 相等续
    {-1, 71, 71, 71, 71, 71, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 71, 71, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 71, 71, 71, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, // 关系式
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 76, 72, 73, 74, 75, 76, 76, 76, 76, -1, -1, -1, 76, -1, -1, -1, 76, 76, 76, -1, -1, -1, -1}, // 关系续
    {-1, 77, 77, 77, 77, 77, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 77, 77, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 77, 77, 77, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, // 加法式
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 78, 79, -1, -1, 80, 80, 80, 80, 80, 80, 80, 80, 80, -1, -1, -1, 80, -1, -1, -1, 80, 80, 80, -1, -1, -1, -1}, // 加法续
    {-1, 81, 81, 81, 81, 81, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 81, 81, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 81, 81, 81, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, // 乘法式
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 85, 85, 82, 83, 85, 85, 85, 85, 85, 85, 85, 85, 85, -1, -1, -1, 85, -1, -1, -1, 85, 85, 85, -1, -1, -1, 84}, // 乘法续
    {-1, 90, 90, 90, 90, 90, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 86, 87, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 88, 89, 90, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, // 一元式
    {-1, 91, 91, 91, 91, 91, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 91, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, // 后缀式
    {-1, 92, 93, 94, 95, 96, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 97, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, // 主式
    {-1, -1, -1, -1, -1, 98, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, // 字符串
    {-1, -1, -1, -1, -1, 99, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, -1, -1, 100, 100, -1, -1, 100, 100, 100, 100, 100, -1, -1, 100}, // 字符串续
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 102, 102, 102, 102, 102, 102, 102, 102, 102, 102, 102, 102, 102, -1, -1, 101, 102, -1, -1, 101, 102, 102, 102, 101, -1, -1, 102}, // 表达后缀序列
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 103, -1, -1, -1, 104, -1, -1, -1, 105, -1, -1, -1}, // 表达后缀
    {-1, 106, 106, 106, 106, 106, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 106, 106, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 106, 106, 106, 107, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}, // 实参可选
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 109, -1, -1, -1, -1, -1, 108, -1, -1, -1, -1}, // 实参续
};
// FOLLOW 集成员标记，用于错误恢复时同步输入。
inline constexpr bool follow[][43] = {
    {true, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false}, // 程序
    {true, false, false, false, false, false, true, true, true, true, false, false, false, false, false, true, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false}, // 外部声明
    {true, false, false, false, false, false, true, true, true, true, false, false, false, false, false, true, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false}, // 外部声明尾
    {true, false, false, false, false, false, true, true, true, true, false, false, false, false, false, true, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false}, // 函数尾
    {false, true, true, true, true, true, true, true, true, true, true, true, false, true, true, false, true, true, false, false, false, false, false, false, false, false, false, false, false, true, true, true, false, true, true, false, false, true, false, false, true, true, false}, // 局部声明
    {true, true, true, true, true, true, true, true, true, true, true, true, false, true, true, true, true, true, false, false, false, false, false, false, false, false, false, false, false, true, true, true, false, true, true, false, false, true, false, false, true, true, false}, // 结构声明尾
    {true, true, true, true, true, true, true, true, true, true, true, true, false, true, true, true, true, true, false, false, false, false, false, false, false, false, false, false, false, true, true, true, false, true, true, false, false, true, false, false, true, true, false}, // 定义后
    {true, true, true, true, true, true, true, true, true, true, true, true, false, true, true, true, true, true, false, false, false, false, false, false, false, false, false, false, false, true, true, true, false, true, true, false, false, true, false, false, true, true, false}, // 对象尾
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, false, false}, // 对象续
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, true, false, false, false, false}, // 初值可选
    {false, true, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false}, // 标量类型
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, false, false, false, false, false}, // 成员序列
    {false, false, false, false, false, false, true, true, true, true, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, false, false, false, false, false}, // 成员声明
    {false, true, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false}, // 成员类型
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, false, false}, // 成员续
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, false, true, true, false, false, false, false}, // 数组可选
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, false, false, false}, // 界限可选
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, false, false, false, false, false, false, false}, // 形参表可选
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, false, false, false, false, false, false, false}, // 形参续
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, false, false, true, false, false, false, false}, // 形参
    {true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, false, false, false, false, false, false, false, false, false, false, false, true, true, true, false, true, true, false, false, true, false, false, true, true, false}, // 块
    {false, true, true, true, true, true, false, false, false, false, true, true, false, true, true, false, true, true, false, false, false, false, false, false, false, false, false, false, false, true, true, true, false, true, true, false, false, true, false, false, true, true, false}, // 局部声明序列
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, false, false, false, false, false}, // 语句序列
    {false, true, true, true, true, true, false, false, false, false, true, true, false, true, true, false, true, true, false, false, false, false, false, false, false, false, false, false, false, true, true, true, false, true, true, false, false, true, false, false, true, true, false}, // 语句
    {false, true, true, true, true, true, false, false, false, false, true, true, false, true, true, false, true, true, false, false, false, false, false, false, false, false, false, false, false, true, true, true, false, true, true, false, false, true, false, false, true, true, false}, // 否则可选
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, false, true, false, false, false, false, false}, // 表达式可选
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, true, true, true, false, false, false, false}, // 表达式
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, true, true, true, false, false, false, false}, // 赋值续
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, true, true, true, false, false, false, false}, // 逻辑或式
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, true, true, true, false, false, false, false}, // 逻辑或续
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, false, false, false, false, true, false, false, false, true, false, false, false, true, true, true, false, false, false, false}, // 逻辑与式
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, false, false, false, false, true, false, false, false, true, false, false, false, true, true, true, false, false, false, false}, // 逻辑与续
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, false, false, false, true, true, false, false, false, true, false, false, false, true, true, true, false, false, false, false}, // 相等式
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, false, false, false, true, true, false, false, false, true, false, false, false, true, true, true, false, false, false, false}, // 相等续
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, false, true, true, true, true, false, false, false, true, false, false, false, true, true, true, false, false, false, false}, // 关系式
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, false, true, true, true, true, false, false, false, true, false, false, false, true, true, true, false, false, false, false}, // 关系续
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, true, true, true, true, true, true, true, true, false, false, false, true, false, false, false, true, true, true, false, false, false, false}, // 加法式
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, true, true, true, true, true, true, true, true, false, false, false, true, false, false, false, true, true, true, false, false, false, false}, // 加法续
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, true, false, false, true, true, true, true, true, true, true, true, true, false, false, false, true, false, false, false, true, true, true, false, false, false, false}, // 乘法式
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, true, false, false, true, true, true, true, true, true, true, true, true, false, false, false, true, false, false, false, true, true, true, false, false, false, false}, // 乘法续
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, true, true, true, true, true, true, true, true, true, true, true, true, false, false, false, true, false, false, false, true, true, true, false, false, false, true}, // 一元式
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, true, true, true, true, true, true, true, true, true, true, true, true, false, false, false, true, false, false, false, true, true, true, false, false, false, true}, // 后缀式
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, true, true, true, true, true, true, true, true, true, true, true, true, false, false, true, true, false, false, true, true, true, true, true, false, false, true}, // 主式
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, true, true, true, true, true, true, true, true, true, true, true, true, false, false, true, true, false, false, true, true, true, true, true, false, false, true}, // 字符串
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, true, true, true, true, true, true, true, true, true, true, true, true, false, false, true, true, false, false, true, true, true, true, true, false, false, true}, // 字符串续
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, true, true, true, true, true, true, true, true, true, true, true, true, false, false, false, true, false, false, false, true, true, true, false, false, false, true}, // 表达后缀序列
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, true, true, true, true, true, true, true, true, true, true, true, true, false, false, true, true, false, false, true, true, true, true, true, false, false, true}, // 表达后缀
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, false, false, false, false, false, false, false}, // 实参可选
    {false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, true, false, false, false, false, false, false, false, false, false, false}, // 实参续
};
inline constexpr const char* names[] = {
    "END_OF_FILE",
    "ID",
    "INT_LITERAL",
    "FLOAT_LITERAL",
    "CHAR_LITERAL",
    "STRING_LITERAL",
    "KW_INT",
    "KW_FLOAT",
    "KW_CHAR",
    "KW_STRUCT",
    "KW_RETURN",
    "KW_IF",
    "KW_ELSE",
    "KW_WHILE",
    "KW_FOR",
    "KW_VOID",
    "PLUS",
    "MINUS",
    "STAR",
    "SLASH",
    "ASSIGN",
    "LT",
    "LE",
    "GT",
    "GE",
    "EQ",
    "NE",
    "AND",
    "OR",
    "NOT",
    "AMP",
    "LPAREN",
    "RPAREN",
    "LBRACE",
    "RBRACE",
    "LBRACKET",
    "RBRACKET",
    "SEMI",
    "COMMA",
    "DOT",
    "KW_BREAK",
    "KW_CONTINUE",
    "PERCENT",
    "程序",
    "外部声明",
    "外部声明尾",
    "函数尾",
    "局部声明",
    "结构声明尾",
    "定义后",
    "对象尾",
    "对象续",
    "初值可选",
    "标量类型",
    "成员序列",
    "成员声明",
    "成员类型",
    "成员续",
    "数组可选",
    "界限可选",
    "形参表可选",
    "形参续",
    "形参",
    "块",
    "局部声明序列",
    "语句序列",
    "语句",
    "否则可选",
    "表达式可选",
    "表达式",
    "赋值续",
    "逻辑或式",
    "逻辑或续",
    "逻辑与式",
    "逻辑与续",
    "相等式",
    "相等续",
    "关系式",
    "关系续",
    "加法式",
    "加法续",
    "乘法式",
    "乘法续",
    "一元式",
    "后缀式",
    "主式",
    "字符串",
    "字符串续",
    "表达后缀序列",
    "表达后缀",
    "实参可选",
    "实参续",
};
static_assert(static_cast<int>(TokenType::END_OF_FILE) == 0, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::ID) == 1, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::INT_LITERAL) == 2, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::FLOAT_LITERAL) == 3, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::CHAR_LITERAL) == 4, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::STRING_LITERAL) == 5, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::KW_INT) == 6, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::KW_FLOAT) == 7, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::KW_CHAR) == 8, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::KW_STRUCT) == 9, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::KW_RETURN) == 10, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::KW_IF) == 11, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::KW_ELSE) == 12, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::KW_WHILE) == 13, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::KW_FOR) == 14, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::KW_VOID) == 15, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::PLUS) == 16, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::MINUS) == 17, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::STAR) == 18, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::SLASH) == 19, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::ASSIGN) == 20, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::LT) == 21, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::LE) == 22, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::GT) == 23, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::GE) == 24, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::EQ) == 25, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::NE) == 26, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::AND) == 27, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::OR) == 28, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::NOT) == 29, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::AMP) == 30, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::LPAREN) == 31, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::RPAREN) == 32, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::LBRACE) == 33, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::RBRACE) == 34, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::LBRACKET) == 35, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::RBRACKET) == 36, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::SEMI) == 37, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::COMMA) == 38, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::DOT) == 39, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::KW_BREAK) == 40, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::KW_CONTINUE) == 41, "请重新生成预测表");
static_assert(static_cast<int>(TokenType::PERCENT) == 42, "请重新生成预测表");
} }
