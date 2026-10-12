// ===== merged from common/diagnostic.cpp =====
#include "minic/diagnostic.hpp"

#include <stdexcept>
#include <utility>

namespace minic {

DiagnosticEngine::DiagnosticEngine(Phase phase, std::size_t max_errors)
    : phase_(phase), max_errors_(max_errors) {
    if (max_errors == 0) throw std::invalid_argument("错误上限必须大于 0");
}

void DiagnosticEngine::report(Level level, const SourceRange& range,
                              const std::string& message, const std::string& code,
                              const std::vector<SourceRange>& related) {
    if (should_stop()) return;
    const bool error = level == Level::Error || level == Level::Fatal;
    Diagnostic entry;
    entry.phase = phase_;
    entry.level = level;
    entry.line = range.begin.line;
    entry.col = range.begin.col;
    entry.message = message;
    entry.code = code;
    entry.range = range;
    entry.related = related;
    if (error && error_count_ + 1 == max_errors_)
        entry.message += "；错误过多，停止本阶段";
    diagnostics_.push_back(std::move(entry));
    if (error) ++error_count_;
    if (level == Level::Fatal) fatal_seen_ = true;
}

std::size_t DiagnosticEngine::error_count() const noexcept { return error_count_; }

bool DiagnosticEngine::should_stop() const noexcept {
    return fatal_seen_ || error_count_ >= max_errors_;
}

const std::vector<Diagnostic>& DiagnosticEngine::diagnostics() const noexcept {
    return diagnostics_;
}

std::vector<Diagnostic> DiagnosticEngine::take_diagnostics() {
    auto result = std::move(diagnostics_);
    diagnostics_.clear();
    error_count_ = 0;
    fatal_seen_ = false;
    return result;
}

}

// ===== merged from common/display.cpp =====
#include "minic/display.hpp"

#include <iomanip>
#include <sstream>
#include <type_traits>

namespace minic {
namespace {

// 将换行、制表符和零字节显示为转义，避免表格被拆行。
std::string escaped(const std::string& value) {
    std::ostringstream text;
    text << '"';
    for (const unsigned char character : value) {
        switch (character) {
        case '\n': text << "\\n"; break;
        case '\r': text << "\\r"; break;
        case '\t': text << "\\t"; break;
        case '\0': text << "\\0"; break;
        case '\\': text << "\\\\"; break;
        case '"': text << "\\\""; break;
        default:
            if (character < 32 || character == 127)
                text << "\\x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(character) << std::dec;
            else text << static_cast<char>(character);
        }
    }
    text << '"';
    return text.str();
}

const char* token_name(TokenType type) {
    static const char* names[] = {
        "END_OF_FILE", "ID", "INT_LITERAL", "FLOAT_LITERAL", "CHAR_LITERAL", "STRING_LITERAL",
        "KW_INT", "KW_FLOAT", "KW_CHAR", "KW_STRUCT", "KW_RETURN", "KW_IF", "KW_ELSE", "KW_WHILE", "KW_FOR", "KW_VOID",
        "PLUS", "MINUS", "STAR", "SLASH", "ASSIGN", "LT", "LE", "GT", "GE", "EQ", "NE", "AND", "OR", "NOT", "AMP",
        "LPAREN", "RPAREN", "LBRACE", "RBRACE", "LBRACKET", "RBRACKET", "SEMI", "COMMA", "DOT",
        "KW_BREAK", "KW_CONTINUE", "KW_SWITCH", "KW_CASE", "KW_DEFAULT", "KW_DO", "KW_TYPEDEF", "KW_UNION", "KW_ENUM",
        "KW_DOUBLE", "KW_LONG", "KW_SHORT", "KW_UNSIGNED", "KW_SIGNED", "KW_STATIC", "KW_EXTERN", "KW_AUTO", "KW_REGISTER",
        "KW_CONST", "KW_VOLATILE", "KW_SIZEOF", "KW_GOTO", "PERCENT", "PLUS_PLUS", "MINUS_MINUS", "PLUS_ASSIGN", "MINUS_ASSIGN",
        "STAR_ASSIGN", "SLASH_ASSIGN", "PERCENT_ASSIGN", "BIT_OR", "BIT_XOR", "BIT_NOT", "SHIFT_LEFT", "SHIFT_RIGHT",
        "AND_ASSIGN", "OR_ASSIGN", "XOR_ASSIGN", "SHIFT_LEFT_ASSIGN", "SHIFT_RIGHT_ASSIGN", "QUESTION", "COLON", "ARROW",
        "ELLIPSIS", "HASH", "HASH_HASH"
    };
    const auto index = static_cast<std::size_t>(type);
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "INVALID_TOKEN";
}

const char* node_name(NodeType type) {
    static const char* names[] = {
        "Program", "FunctionDecl", "FunctionDef", "ParamDecl", "VarDecl", "StructDef", "MemberDecl", "Block", "ExprStmt",
        "If", "While", "For", "Return", "Empty", "Identifier", "IntLiteral", "FloatLiteral", "CharLiteral", "StringLiteral",
        "Assign", "BinaryOp", "UnaryOp", "Call", "ImplicitCast", "ArrayAccess", "MemberAccess", "Break", "Continue", "Switch",
        "Case", "Default", "DoWhile", "Conditional", "Cast", "Sizeof", "UnionDef", "EnumDef", "EnumMember", "TypedefDecl",
        "InitList", "Label", "Goto", "Error"
    };
    const auto index = static_cast<std::size_t>(type);
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "InvalidNode";
}

std::string type_text(const TypePtr& type, std::size_t depth = 0) {
    if (!type) return "未标注";
    if (depth > 64) return "类型过深";
    static const char* names[] = {"Unknown", "Error", "void", "char", "short", "int", "long", "float", "double", "long double",
                                 "address", "array", "function", "struct", "union", "enum", "named"};
    const auto index = static_cast<std::size_t>(type->kind);
    std::string value = index < sizeof(names) / sizeof(names[0]) ? names[index] : "InvalidType";
    if (type->kind == TypeKind::Address) value = "地址(" + type_text(type->base, depth + 1) + ")";
    else if (type->kind == TypeKind::Array)
        value = type_text(type->base, depth + 1) + "[" + (type->array_length ? std::to_string(*type->array_length) : "") + "]";
    else if (type->kind == TypeKind::Function) {
        value = type_text(type->base, depth + 1) + "(";
        for (std::size_t i = 0; i < type->params.size(); ++i) {
            if (i) value += ", ";
            value += type_text(type->params[i], depth + 1);
        }
        if (type->variadic) value += type->params.empty() ? "..." : ", ...";
        value += ")";
    } else if (type->kind == TypeKind::Struct || type->kind == TypeKind::Union || type->kind == TypeKind::Enum)
        value += " " + type->name + "#" + std::to_string(type->record_id);
    if (type->is_unsigned) value = "unsigned " + value;
    if (type->is_const) value = "const " + value;
    return value;
}

void ast(const ASTNode& node, std::ostream& output, std::size_t depth) {
    output << std::string(depth * 2, ' ') << node_name(node.kind);
    if (!node.name.empty()) output << ' ' << escaped(node.name);
    output << " : " << type_text(node.type);
    if (node.symbol_id != invalid_id) output << " %s" << node.symbol_id;
    if (node.scope_id != invalid_id) output << " scope=" << node.scope_id;
    output << '\n';
    if (depth >= 512) { output << "语法树过深，停止展示\n"; return; }
    for (const auto& child : node.children) {
        if (child) ast(*child, output, depth + 1);
        else output << std::string((depth + 1) * 2, ' ') << "<空孩子>\n";
    }
}

void quads(const std::vector<Quadruple>& list, std::ostream& output) {
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& quad = list[i];
        output << i << ": (" << quad.op << ", " << quad.arg1 << ", " << quad.arg2 << ", " << quad.result << ")\n";
    }
}

}

void print_tokens(const std::vector<Token>& tokens, std::ostream& output) {
    output << "种别\t原文\t行:列\n";
    for (const auto& token : tokens)
        output << token_name(token.type) << '\t' << escaped(token.lexeme) << '\t' << token.line << ':' << token.col << '\n';
}

void print_ast(const Program& program, std::ostream& output) { ast(program, output, 0); }

std::string token_type_name(TokenType type) { return token_name(type); }
std::string ast_node_name(NodeType type) { return node_name(type); }
std::string describe_type(const TypePtr& type) { return type_text(type); }

void print_symbols(const SymbolTableData& symbols, std::ostream& output) {
    output << "符号编号\t名字\t类别\t类型\t作用域\t位置\n";
    static const char* kinds[] = {"变量", "形参", "数组", "函数", "类型别名", "枚举常量"};
    for (const auto& entry : symbols.symbols) {
        const auto kind = static_cast<std::size_t>(entry.kind);
        output << "%s" << entry.id << '\t' << entry.name << '\t'
               << (kind < sizeof(kinds) / sizeof(kinds[0]) ? kinds[kind] : "无效") << '\t'
               << type_text(entry.type) << '\t' << entry.scope << '\t' << entry.range.file << ':' << entry.line << ':' << entry.col << '\n';
    }
    for (const auto& record : symbols.records) {
        output << "记录 #" << record.id << ' ' << record.tag << " scope=" << record.scope
               << (record.is_complete ? " 已定义" : " 未完整") << '\n';
        for (const auto& member : record.members)
            output << "  " << member.name << " : " << type_text(member.type) << " offset="
                   << (member.offset ? std::to_string(*member.offset) : "未布局") << '\n';
        for (const auto& value : record.enumerators) output << "  " << value.name << "=" << value.value << " %s" << value.symbol_id << '\n';
    }
    for (const auto& scope : symbols.scopes)
        output << "作用域 " << scope.id << " parent=" << (scope.parent == invalid_id ? "无" : std::to_string(scope.parent))
               << " 普通名字=" << scope.symbols.size() << " 标签=" << scope.tags.size() << '\n';
}

void print_ir(const IRProgram& program, const SymbolTableData& symbols, std::ostream& output) {
    output << "常量池\n";
    for (const auto& constant : program.constants.entries) {
        output << "%c" << constant.id << " : " << type_text(constant.type) << " = ";
        switch (constant.value.kind) {
        case ConstantKind::Empty: output << "未解码"; break;
        case ConstantKind::Integer: output << constant.value.integer; break;
        case ConstantKind::Unsigned: output << constant.value.unsigned_integer; break;
        case ConstantKind::Floating: output << constant.value.floating; break;
        case ConstantKind::Text: output << escaped(constant.value.text); break;
        }
        output << '\n';
    }
    output << "全局初始化\n";
    quads(program.global_initializers, output);
    for (const auto& function : program.functions) {
        output << "函数 %s" << function.symbol_id;
        if (function.symbol_id < symbols.symbols.size()) output << ' ' << symbols.symbols[function.symbol_id].name;
        output << '\n';
        for (const auto& temporary : function.temporaries) output << "  " << temporary.name << " : " << type_text(temporary.type) << '\n';
        quads(function.quads, output);
    }
}

void print_diagnostics(const std::vector<Diagnostic>& diagnostics, std::ostream& output) {
    static const char* phases[] = {"预处理", "词法", "语法", "语义", "中间代码", "运行"};
    static const char* levels[] = {"说明", "警告", "错误", "致命错误"};
    for (const auto& entry : diagnostics) {
        const auto phase = static_cast<std::size_t>(entry.phase);
        const auto level = static_cast<std::size_t>(entry.level);
        output << entry.range.file << ':' << entry.line << ':' << entry.col << " ["
               << (phase < sizeof(phases) / sizeof(phases[0]) ? phases[phase] : "无效阶段") << '/'
               << (level < sizeof(levels) / sizeof(levels[0]) ? levels[level] : "无效级别") << "] "
               << entry.code << ' ' << entry.message << '\n';
        for (const auto& related : entry.related)
            output << "  关联位置：" << related.file << ':' << related.begin.line << ':' << related.begin.col << '\n';
    }
}

}

// ===== merged from common/compilation_result.cpp =====
#include "minic/compiler.hpp"

namespace minic {

bool CompilationResult::ok() const {
    if (has_errors(diagnostics) || !lexical || !lexical->ok()) return false;
    if (target == CompileTarget::Tokens) return !syntax && !semantic && !ir;
    if (!syntax || !syntax->ok()) return false;
    if (target == CompileTarget::Parse) return !semantic && !ir;
    if (!semantic || !semantic->ok()) return false;
    if (target == CompileTarget::Check) return !ir;
    return target == CompileTarget::IR && ir && ir->ok();
}

}

// ===== merged from preprocessor.cpp =====
// ===== merged from frontend/preprocessor.cpp =====
#include "minic/preprocessor.hpp"
#include "minic/diagnostic.hpp"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace minic {
namespace {
bool name_start(unsigned char c) { return std::isalpha(c) || c == '_'; }
bool name_part(unsigned char c) { return std::isalnum(c) || c == '_'; }
std::string trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    return begin == std::string::npos ? "" : value.substr(begin, value.find_last_not_of(" \t\r\n") - begin + 1);
}
std::pair<std::string, std::string> split(const std::string& value) {
    std::size_t end = 0; while (end < value.size() && name_part(static_cast<unsigned char>(value[end]))) ++end;
    return {value.substr(0, end), trim(value.substr(end))};
}
struct Macro {
    std::vector<std::string> parameters; // 函数宏的参数名，按声明顺序保存。
    std::string body; // 宏的替换文字。
    bool function = false; // 是否需要括号实参。
};
struct Conditional {
    bool parent = true; // 外层是否启用。
    bool selected = false; // 本条件组是否已有分支被选择。
    bool active = false; // 当前分支是否启用。
    bool saw_else = false; // 是否已经出现 else。
};

// 条件表达式使用固定的有符号 32 位模型，未选择的分支只检查语法。
class ConditionParser {
    const std::string& text_; // 已展开的条件文字。
    std::size_t position_ = 0; // 下一个待读取字符。
    std::size_t depth_ = 0; // 当前递归深度。
    void skip() { while (position_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[position_]))) ++position_; }
    bool take(const std::string& token) {
        skip(); if (text_.compare(position_, token.size(), token) != 0) return false;
        position_ += token.size(); return true;
    }
    static long long checked(long long value) {
        if (value < -2147483648LL || value > 2147483647LL) throw std::runtime_error("条件整数超出有符号 32 位范围");
        return value;
    }
    static long long signed_bits(unsigned value) { return value > 2147483647U ? static_cast<long long>(value) - 4294967296LL : value; }
    long long unary(bool evaluate) {
        if (++depth_ > 128) throw std::runtime_error("条件表达式嵌套过深");
        struct Exit { std::size_t& depth; ~Exit() { --depth; } } exit{depth_};
        if (take("!")) return !unary(evaluate);
        if (take("~")) { const auto value = unary(evaluate); return evaluate ? signed_bits(~static_cast<unsigned>(value)) : 0; }
        if (take("+")) return unary(evaluate);
        if (take("-")) { const auto value = unary(evaluate); return evaluate ? checked(-value) : 0; }
        if (take("(")) { const auto value = expression(1, evaluate); if (!take(")")) throw std::runtime_error("条件缺少右括号"); return value; }
        skip(); const auto begin = position_;
        while (position_ < text_.size() && name_part(static_cast<unsigned char>(text_[position_]))) ++position_;
        if (begin == position_) throw std::runtime_error("条件表达式不完整或含有不支持的字面量");
        auto token = text_.substr(begin, position_ - begin);
        if (name_start(static_cast<unsigned char>(token[0]))) return 0;
        if (!token.empty() && (token.back() == 'l' || token.back() == 'L')) token.pop_back();
        std::size_t used = 0; const auto value = std::stoll(token, &used, 0);
        if (used != token.size()) throw std::runtime_error("条件整数常量无效（暂不支持 unsigned 后缀）");
        return checked(value);
    }
    std::pair<std::string, int> operation() {
        skip();
        for (const auto& op : {"||", "&&", "==", "!=", "<=", ">=", "<<", ">>"})
            if (text_.compare(position_, 2, op) == 0) {
                const std::string token = op;
                return {token, token == "||" ? 2 : token == "&&" ? 3 : (token == "==" || token == "!=") ? 7 : (token == "<<" || token == ">>") ? 9 : 8};
            }
        if (position_ == text_.size()) return {"", 0};
        switch (text_[position_]) {
        case '|': return {"|", 4}; case '^': return {"^", 5}; case '&': return {"&", 6};
        case '<': return {"<", 8}; case '>': return {">", 8};
        case '+': return {"+", 10}; case '-': return {"-", 10};
        case '*': return {"*", 11}; case '/': return {"/", 11}; case '%': return {"%", 11};
        default: return {"", 0};
        }
    }
    long long expression(int precedence, bool evaluate) {
        if (++depth_ > 128) throw std::runtime_error("条件表达式嵌套过深");
        struct Exit { std::size_t& depth; ~Exit() { --depth; } } exit{depth_};
        auto left = unary(evaluate);
        for (;;) {
            const auto op = operation(); if (op.second < precedence || op.second == 0) break;
            position_ += op.first.size();
            const bool execute = evaluate && !(op.first == "&&" && !left) && !(op.first == "||" && left);
            const auto right = expression(op.second + 1, execute);
            if (!evaluate) { left = 0; continue; }
            const auto& token = op.first;
            if (token == "&&") left = left && right;
            else if (token == "||") left = left || right;
            else if (token == "==") left = left == right;
            else if (token == "!=") left = left != right;
            else if (token == "<") left = left < right;
            else if (token == ">") left = left > right;
            else if (token == "<=") left = left <= right;
            else if (token == ">=") left = left >= right;
            else if (token == "+") left = checked(left + right);
            else if (token == "-") left = checked(left - right);
            else if (token == "*") left = checked(left * right);
            else if (token == "/" || token == "%") {
                if (!right) throw std::runtime_error("条件表达式除零");
                if (left == -2147483648LL && right == -1) throw std::runtime_error("条件表达式除法溢出");
                left = token == "/" ? left / right : left % right;
            } else if (token == "<<" || token == ">>") {
                if (right < 0 || right >= 32) throw std::runtime_error("条件移位数量超出范围");
                if (token == "<<") { if (left < 0) throw std::runtime_error("条件负数不能左移"); left = checked(left * (static_cast<long long>(1) << right)); }
                else { const auto divisor = static_cast<long long>(1) << right; left = left >= 0 ? left / divisor : -((-left + divisor - 1) / divisor); }
            } else {
                const auto a = static_cast<unsigned>(left), b = static_cast<unsigned>(right);
                left = signed_bits(token == "&" ? a & b : token == "|" ? a | b : a ^ b);
            }
        }
        if (precedence == 1 && take("?")) {
            const auto yes = expression(1, evaluate && left != 0);
            if (!take(":")) throw std::runtime_error("条件运算符缺少冒号");
            const auto no = expression(1, evaluate && left == 0);
            left = left ? yes : no;
        }
        return left;
    }
public:
    explicit ConditionParser(const std::string& text) : text_(text) {}
    bool run() { const auto value = expression(1, true); skip(); if (position_ != text_.size()) throw std::runtime_error("条件表达式含有不支持的尾部内容"); return value != 0; }
};

class Processor {
    std::unordered_map<std::string, Macro> macros_; // 当前已定义的宏。
    DiagnosticEngine diagnostics_{Phase::Preprocess}; // 本阶段诊断。
    PreprocessResult result_; // 输出源码和原始行映射。
    SourceRange location_; // 当前处理指令的原始位置。
    std::size_t expansions_ = 0; // 本次已展开的宏数量。

    // 跳过完整字符或字符串字面量，保留里面的宏名字。
    std::size_t quoted_end(const std::string& text, std::size_t start) const {
        const char quote = text[start];
        for (std::size_t i = start + 1; i < text.size(); ++i) {
            if (text[i] == '\\') { ++i; continue; }
            if (text[i] == quote) return i + 1;
        }
        throw std::runtime_error("预处理字面量缺少结束引号");
    }

    struct PPToken {
        std::string text; // 一个预处理单词的原文。
        std::string space; // 这个单词前的空白。
        std::unordered_set<std::string> hidden; // 本单词不能再次展开的宏名。
        bool paste = false; // 是否是宏体中的拼接运算符。
    };
    using Tokens = std::vector<PPToken>;

    // 这里只分预处理单词，不判断 C 语法，也不替代正式词法模块。
    Tokens tokenize(const std::string& text) const {
        Tokens result;
        for (std::size_t i = 0; i < text.size();) {
            const auto space_begin = i;
            while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
            const auto space = text.substr(space_begin, i - space_begin);
            if (i == text.size()) break;
            const auto begin = i;
            if (text[i] == '"' || text[i] == '\'') i = quoted_end(text, i);
            else if (text[i] == 'L' && i + 1 < text.size() && (text[i + 1] == '"' || text[i + 1] == '\'')) i = quoted_end(text, i + 1);
            else if (std::isdigit(static_cast<unsigned char>(text[i])) ||
                     (text[i] == '.' && i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1])))) {
                ++i;
                while (i < text.size() && (name_part(static_cast<unsigned char>(text[i])) || text[i] == '.' ||
                    ((text[i] == '+' || text[i] == '-') && (text[i - 1] == 'e' || text[i - 1] == 'E' || text[i - 1] == 'p' || text[i - 1] == 'P')))) ++i;
            } else if (name_start(static_cast<unsigned char>(text[i]))) {
                while (i < text.size() && name_part(static_cast<unsigned char>(text[i]))) ++i;
            } else {
                bool matched = false;
                for (const auto& punct : {"<<=", ">>=", "...", "##", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=", "&&", "||", "*=", "/=", "%=", "+=", "-=", "&=", "^=", "|="}) {
                    const std::string token = punct;
                    if (text.compare(i, token.size(), token) == 0) { i += token.size(); matched = true; break; }
                }
                if (!matched) ++i;
            }
            result.push_back({text.substr(begin, i - begin), space, {}});
        }
        return result;
    }

    std::string render(const Tokens& tokens) const {
        std::string result, previous;
        for (const auto& token : tokens) {
            if (token.text.empty()) continue;
            auto space = token.space;
            if (space.empty() && !previous.empty()) {
                // 防止替换结果中的两个单词意外变成 ++、名字或注释。
                const auto pair = tokenize(previous + token.text);
                if (pair.size() != 2 || pair[0].text != previous || pair[1].text != token.text ||
                    (previous == "/" && (token.text == "/" || token.text == "*"))) space = " ";
            }
            result += space + token.text; previous = token.text;
            if (result.size() > 16 * 1024 * 1024) throw std::runtime_error("宏展开结果过大");
        }
        return result;
    }

    // 字符串化使用未展开的实参，只合并单词之间的空白。
    std::string stringify(const Tokens& tokens) const {
        std::string value;
        for (const auto& token : tokens) {
            if (!value.empty() && !token.space.empty()) value += ' ';
            for (const char c : token.text) {
                if (c == '"' || c == '\\') value += '\\';
                value += c;
            }
        }
        return "\"" + value + "\"";
    }

    Tokens expand_tokens(Tokens tokens, std::size_t depth = 0) {
        if (depth > 128) throw std::runtime_error("宏展开超过数量或深度限制");
        for (std::size_t i = 0; i < tokens.size();) {
            const auto invocation = tokens[i];
            const auto found = macros_.find(invocation.text);
            if (found == macros_.end() || invocation.hidden.count(invocation.text)) { ++i; continue; }
            const auto macro = found->second;
            auto inherited = invocation.hidden;
            std::size_t end = i + 1;
            std::unordered_map<std::string, Tokens> raw, expanded;
            if (macro.function) {
                if (end == tokens.size() || tokens[end].text != "(") { ++i; continue; }
                std::vector<Tokens> arguments(1); std::size_t nesting = 0;
                for (++end; end < tokens.size(); ++end) {
                    const auto& token = tokens[end];
                    if (token.text == ")" && !nesting) break;
                    if (token.text == "," && !nesting) { arguments.emplace_back(); continue; }
                    if (token.text == "(") ++nesting;
                    else if (token.text == ")") --nesting;
                    arguments.back().push_back(token);
                }
                if (end == tokens.size()) throw std::runtime_error("函数宏调用缺少右括号");
                // 函数宏可能接上宏体以外的括号；只继承首单词和右括号共有的禁用宏。
                for (auto hidden = inherited.begin(); hidden != inherited.end();) {
                    if (!tokens[end].hidden.count(*hidden)) hidden = inherited.erase(hidden); else ++hidden;
                }
                ++end;
                if (arguments.size() == 1 && arguments[0].empty() && macro.parameters.empty()) arguments.clear();
                if (arguments.size() != macro.parameters.size()) throw std::runtime_error("函数宏实参数量不匹配：" + invocation.text);
                for (std::size_t a = 0; a < arguments.size(); ++a) raw[macro.parameters[a]] = std::move(arguments[a]);
            }
            if (++expansions_ > 100000 || invocation.hidden.size() >= 128) throw std::runtime_error("宏展开超过数量或深度限制");
            const auto body = tokenize(macro.body);
            Tokens replacement;
            for (std::size_t b = 0; b < body.size(); ++b) {
                const auto& token = body[b];
                if (macro.function && token.text == "#") {
                    const auto parameter = raw.find(body.at(++b).text);
                    if (parameter == raw.end()) throw std::runtime_error("字符串化后面必须是宏参数");
                    replacement.push_back({stringify(parameter->second), token.space, {}}); continue;
                }
                const auto argument = raw.find(token.text);
                if (argument == raw.end()) { auto literal = token; literal.paste = token.text == "##"; replacement.push_back(std::move(literal)); continue; }
                const bool pasted = (b && body[b - 1].text == "##") || (b + 1 < body.size() && body[b + 1].text == "##");
                Tokens value;
                if (pasted) value = argument->second;
                else {
                    auto cached = expanded.find(token.text);
                    if (cached == expanded.end()) cached = expanded.emplace(token.text, expand_tokens(argument->second, depth + 1)).first;
                    value = cached->second;
                }
                if (value.empty()) { if (pasted) replacement.push_back({"", token.space, {}}); }
                else { value.front().space = token.space; replacement.insert(replacement.end(), value.begin(), value.end()); }
            }
            // ## 合并左右各一个单词；空实参保留另一个单词。
            for (std::size_t b = 0; b < replacement.size();) {
                if (!replacement[b].paste) { ++b; continue; }
                if (!b || b + 1 == replacement.size()) throw std::runtime_error("拼接运算符缺少左右单词");
                auto joined = replacement[b - 1];
                joined.paste = false;
                joined.text += replacement[b + 1].text;
                joined.hidden.insert(replacement[b + 1].hidden.begin(), replacement[b + 1].hidden.end());
                if (!joined.text.empty()) {
                    const auto check = tokenize(joined.text);
                    if (check.size() != 1 || check[0].text != joined.text || joined.text == "/*" || joined.text == "//")
                        throw std::runtime_error("拼接结果不是一个有效预处理单词");
                }
                replacement.erase(replacement.begin() + static_cast<std::ptrdiff_t>(b - 1), replacement.begin() + static_cast<std::ptrdiff_t>(b + 2));
                replacement.insert(replacement.begin() + static_cast<std::ptrdiff_t>(b - 1), std::move(joined));
                --b;
            }
            for (auto& token : replacement) {
                token.hidden.insert(inherited.begin(), inherited.end()); token.hidden.insert(invocation.text);
            }
            if (!replacement.empty()) replacement.front().space = invocation.space;
            else if (end < tokens.size()) tokens[end].space = invocation.space + tokens[end].space;
            tokens.erase(tokens.begin() + static_cast<std::ptrdiff_t>(i), tokens.begin() + static_cast<std::ptrdiff_t>(end));
            tokens.insert(tokens.begin() + static_cast<std::ptrdiff_t>(i), replacement.begin(), replacement.end());
            if (tokens.size() > 1000000) throw std::runtime_error("宏展开单词过多");
            // 从替换起点继续扫描，函数宏可以接上后面源码的括号实参。
        }
        return tokens;
    }

    std::string expand(const std::string& text) {
        if (text.size() > 16 * 1024 * 1024) throw std::runtime_error("宏展开输入过大");
        return render(expand_tokens(tokenize(text)));
    }

    bool condition(std::string text) {
        // defined 不展开操作数，先替换为 0/1，再展开普通对象宏。
        std::string replaced;
        for (std::size_t i = 0; i < text.size();) {
            if (!name_start(static_cast<unsigned char>(text[i]))) { replaced += text[i++]; continue; }
            const auto start = i++; while (i < text.size() && name_part(static_cast<unsigned char>(text[i]))) ++i;
            const auto token = text.substr(start, i - start);
            if (token != "defined") { replaced += token; continue; }
            while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
            const bool parentheses = i < text.size() && text[i] == '('; if (parentheses) ++i;
            while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
            const auto begin = i; if (i == text.size() || !name_start(static_cast<unsigned char>(text[i]))) throw std::runtime_error("defined 缺少宏名");
            while (i < text.size() && name_part(static_cast<unsigned char>(text[i]))) ++i;
            replaced += macros_.count(text.substr(begin, i - begin)) ? '1' : '0';
            while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
            if (parentheses && (i == text.size() || text[i++] != ')')) throw std::runtime_error("defined 缺少右括号");
        }
        text = trim(expand(replaced));
        return ConditionParser(text).run();
    }

    void define(const std::string& text) {
        std::size_t p = 0;
        if (text.empty() || !name_start(static_cast<unsigned char>(text[0]))) throw std::runtime_error("define 缺少有效宏名");
        while (p < text.size() && name_part(static_cast<unsigned char>(text[p]))) ++p;
        const auto name = text.substr(0, p); Macro macro;
        if (p < text.size() && text[p] == '(') {
            macro.function = true; const auto end = text.find(')', ++p);
            if (end == std::string::npos) throw std::runtime_error("宏参数列表不完整");
            const auto parameters = trim(text.substr(p, end - p)); std::istringstream input(parameters); std::string parameter;
            if (!parameters.empty() && parameters.back() == ',') throw std::runtime_error("宏参数名不能为空");
            while (std::getline(input, parameter, ',')) {
                parameter = trim(parameter);
                if (parameter.empty() || !name_start(static_cast<unsigned char>(parameter[0])) ||
                    !std::all_of(parameter.begin(), parameter.end(), [](unsigned char c) { return name_part(c); }) ||
                    std::find(macro.parameters.begin(), macro.parameters.end(), parameter) != macro.parameters.end())
                    throw std::runtime_error("宏参数名无效或重复");
                macro.parameters.push_back(parameter);
            }
            p = end + 1;
        }
        macro.body = trim(text.substr(p));
        const auto body = tokenize(macro.body);
        for (std::size_t i = 0; i < body.size(); ++i) {
            if (body[i].text == "##" && (!i || i + 1 == body.size() || body[i - 1].text == "##" || body[i + 1].text == "##"))
                throw std::runtime_error("拼接运算符缺少左右单词");
            if (macro.function && body[i].text == "#" && (i + 1 == body.size() ||
                std::find(macro.parameters.begin(), macro.parameters.end(), body[i + 1].text) == macro.parameters.end()))
                throw std::runtime_error("字符串化后面必须是宏参数");
        }
        const auto previous = macros_.find(name);
        if (previous != macros_.end() && (previous->second.body != macro.body || previous->second.parameters != macro.parameters || previous->second.function != macro.function))
            throw std::runtime_error("宏重复定义且内容不同：" + name);
        macros_[name] = std::move(macro);
    }

    std::string strip_comments(const std::string& line, bool& block) {
        std::string output;
        for (std::size_t i = 0; i < line.size();) {
            if (block) { const auto end = line.find("*/", i); if (end == std::string::npos) break; block = false; i = end + 2; continue; }
            if (line[i] == '"' || line[i] == '\'') { const auto end = quoted_end(line, i); output += line.substr(i, end - i); i = end; continue; }
            if (line.substr(i, 2) == "//") break;
            if (line.substr(i, 2) == "/*") { output += ' '; block = true; i += 2; continue; }
            output += line[i++];
        }
        return output;
    }

    void emit(const std::string& line, const std::string& file, unsigned number) {
        if (result_.source.size() + line.size() > 16 * 1024 * 1024) throw std::runtime_error("预处理结果超过 16 MiB");
        result_.source += line + '\n'; result_.lines.push_back({file, number});
    }

    void file(const std::string& source, const std::string& filename) {
        if (source.size() > 16 * 1024 * 1024) throw std::runtime_error("单个输入文件超过 16 MiB");
        std::istringstream input(source); std::string line; unsigned number = 0;
        std::vector<Conditional> stack; bool block = false;
        while (std::getline(input, line) && !diagnostics_.should_stop()) {
            const auto first = ++number;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            while (!line.empty() && line.back() == '\\') {
                line.pop_back(); std::string next;
                if (!std::getline(input, next)) throw std::runtime_error("文件末尾的续行不完整");
                ++number; if (!next.empty() && next.back() == '\r') next.pop_back(); line += next;
            }
            location_.file = filename; location_.begin.line = first; location_.end.line = number;
            const auto cleaned = strip_comments(line, block); const auto text = trim(cleaned);
            const bool active = stack.empty() || stack.back().active;
            if (text.empty() || text[0] != '#') { emit(active ? expand(cleaned) : "", filename, first); continue; }
            const auto directive = split(trim(text.substr(1))); const auto& name = directive.first; const auto& argument = directive.second;
            if (name == "if" || name == "ifdef" || name == "ifndef") {
                if (name != "if" && (argument.empty() || !name_start(static_cast<unsigned char>(argument[0])) ||
                    !std::all_of(argument.begin(), argument.end(), [](unsigned char c) { return name_part(c); })))
                    throw std::runtime_error("ifdef/ifndef 需要一个有效宏名");
                bool selected = false;
                if (active) selected = name == "if" ? condition(argument) : name == "ifdef" ? macros_.count(argument) != 0 : macros_.count(argument) == 0;
                stack.push_back({active, selected, active && selected, false});
            } else if (name == "elif" || name == "else") {
                if (stack.empty() || stack.back().saw_else) throw std::runtime_error("elif/else 缺少 if 或位于 else 后面");
                if (name == "else" && !argument.empty()) throw std::runtime_error("else 后面不能有参数");
                auto& current = stack.back();
                const bool selected = current.parent && !current.selected && (name == "else" || condition(argument));
                current.active = selected; current.selected = current.selected || selected; current.saw_else = name == "else";
            } else if (name == "endif") {
                if (stack.empty()) throw std::runtime_error("endif 缺少 if");
                if (!argument.empty()) throw std::runtime_error("endif 后面不能有参数");
                stack.pop_back();
            } else if (active && name == "define") define(argument);
            else if (active && name == "undef") {
                if (argument.empty() || !name_start(static_cast<unsigned char>(argument[0])) ||
                    !std::all_of(argument.begin(), argument.end(), [](unsigned char c) { return name_part(c); }))
                    throw std::runtime_error("undef 需要一个有效宏名");
                macros_.erase(argument);
            }
            else if (active && name == "include") {
                throw std::runtime_error("本项目不支持 #include 头文件，请把声明写在当前源文件中");
            } else if (active && name == "error") throw std::runtime_error("error 指令：" + argument);
            else if (active && !name.empty()) throw std::runtime_error("暂不支持预处理指令：" + name);
            emit("", filename, first);
        }
        if (block) throw std::runtime_error("块注释没有结束");
        if (!stack.empty()) throw std::runtime_error("条件指令缺少 endif");
    }
public:
    PreprocessResult run(const std::string& source, const std::string& filename) {
        try { file(source, filename); }
        catch (const std::exception& error) { diagnostics_.report(Level::Error, location_, error.what(), "PP_FAILURE"); }
        result_.diagnostics = diagnostics_.take_diagnostics(); return std::move(result_);
    }
};
}
PreprocessResult preprocess(const std::string& source, const std::string& filename) {
    return Processor().run(source, filename);
}
}
