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
        "ELLIPSIS"
    };
    const auto index = static_cast<std::size_t>(type);
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "INVALID_TOKEN";
}

const char* node_name(NodeType type) {
    static const char* names[] = {
        "Program", "FunctionDecl", "FunctionDef", "ParamDecl", "VarDecl", "StructDef", "MemberDecl", "Block", "ExprStmt",
        "If", "While", "For", "Return", "Empty", "Identifier", "IntLiteral", "FloatLiteral", "CharLiteral", "StringLiteral",
        "Assign", "BinaryOp", "UnaryOp", "Call", "ImplicitCast", "ArrayAccess", "MemberAccess", "Break", "Continue", "Switch",
        "Case", "Default", "DoWhile", "Conditional", "Cast", "Sizeof", "TypedefDecl",
        "InitList", "Label", "Goto", "Error"
    };
    const auto index = static_cast<std::size_t>(type);
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "InvalidNode";
}

std::string type_text(const TypePtr& type, std::size_t depth = 0) {
    if (!type) return "未标注";
    if (depth > 64) return "类型过深";
    static const char* names[] = {"Unknown", "Error", "void", "char", "short", "int", "long", "float", "double", "long double",
                                 "address", "array", "function", "struct", "named"};
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
    } else if (type->kind == TypeKind::Struct)
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
    static const char* kinds[] = {"变量", "形参", "数组", "函数", "类型别名"};
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
    static const char* phases[] = {"词法", "语法", "语义", "中间代码", "运行"};
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
