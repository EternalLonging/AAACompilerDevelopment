#include "minic/completion.hpp"
#include "minic/compiler.hpp"
#include "minic/display.hpp"
#include "minic/lexer.hpp"
#include "minic/symbol_table.hpp"
#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace minic {
namespace {
bool word(unsigned char c) { return std::isalnum(c) || c == '_'; }

// 确认光标位于代码中，避免在注释和字面量中弹出名字列表。
bool code_position(const std::string& source, std::size_t cursor) {
    enum class State { Code, Line, Block, String, Char } state = State::Code;
    for (std::size_t i = 0; i < cursor; ++i) {
        const char c = source[i], next = i + 1 < cursor ? source[i + 1] : '\0';
        if (state == State::Line) { if (c == '\n' || c == '\r') state = State::Code; }
        else if (state == State::Block) { if (c == '*' && next == '/') { state = State::Code; ++i; } }
        else if (state == State::String || state == State::Char) {
            if (c == '\\') ++i;
            else if ((state == State::String && c == '"') || (state == State::Char && c == '\'')) state = State::Code;
        } else if (c == '/' && next == '/') { state = State::Line; ++i; }
        else if (c == '/' && next == '*') { state = State::Block; ++i; }
        else if (c == '"') state = State::String;
        else if (c == '\'') state = State::Char;
    }
    return state == State::Code;
}

// 补齐尚未闭合的括号；只用于补全候选分析，不作为正式编译结果。
std::string repaired(const std::string& prefix, const std::string& ending) {
    std::vector<char> closes;
    const auto lexical = lex(prefix);
    for (const auto& token : lexical.tokens) {
        if (token.type == TokenType::LBRACE) closes.push_back('}');
        else if (token.type == TokenType::LPAREN) closes.push_back(')');
        else if (token.type == TokenType::LBRACKET) closes.push_back(']');
        else if ((token.type == TokenType::RBRACE || token.type == TokenType::RPAREN || token.type == TokenType::RBRACKET) && !closes.empty()) closes.pop_back();
    }
    auto text = prefix + ending;
    for (auto it = closes.rbegin(); it != closes.rend(); ++it) {
        if (*it == '}' && !text.empty() && text.back() != ';' && text.back() != '}') text += ';';
        text += *it;
    }
    return text;
}
}

CompletionResult complete(const std::string& source, std::size_t cursor) {
    CompletionResult result;
    result.end = std::min(cursor, source.size()); result.begin = result.end;
    if (!code_position(source, result.end)) return result;
    while (result.begin && word(static_cast<unsigned char>(source[result.begin - 1]))) --result.begin;
    const auto prefix = source.substr(result.begin, result.end - result.begin);
    std::unordered_map<std::string, std::size_t> scope_ranks; // 每个符号离当前作用域有几层，越小越靠前。
    const auto before = source.substr(0, result.begin);
    const auto lexical = lex(before);
    const auto& tokens = lexical.tokens;
    const bool member = tokens.size() >= 3 &&
        (tokens[tokens.size() - 2].type == TokenType::DOT || tokens[tokens.size() - 2].type == TokenType::ARROW) &&
        tokens[tokens.size() - 3].type == TokenType::ID;
    auto compiled = compile(source, "<editor>", CompileTarget::Check);
    if (!compiled.syntax || !compiled.syntax->root) {
        const auto cut = member ? tokens[tokens.size() - 2].range.begin.offset : result.begin;
        for (const auto& ending : {std::string(";"), std::string("0;"), std::string("0")} ) {
            auto candidate = compile(repaired(source.substr(0, cut), ending), "<editor>", CompileTarget::Check);
            if (candidate.syntax && candidate.syntax->root && candidate.semantic) {
                compiled = std::move(candidate); result.recovered = true; break;
            }
        }
    }
    if (compiled.semantic) {
        const auto& data = compiled.semantic->symbols;
        ScopeId scope = 0;
        const auto scope_offset = result.recovered && compiled.syntax && compiled.syntax->root ?
            std::min(result.begin, compiled.syntax->root->range.end.offset ? compiled.syntax->root->range.end.offset - 1 : 0) : result.begin;
        for (const auto& entry : data.scopes) {
            if (entry.id != 0 && entry.range.begin.offset <= scope_offset && scope_offset < entry.range.end.offset &&
                (scope == 0 || entry.range.begin.offset >= data.scopes[scope].range.begin.offset)) scope = entry.id;
        }
        // 光标正好在下一条声明开头时，该声明还没有进入可见范围。
        SourceLocation location; location.offset = result.begin ? result.begin - 1 : 0;
        if (member) {
            const auto& name = tokens[tokens.size() - 3].lexeme;
            for (const auto id : visible_symbols(data, name, scope, location)) {
                const auto& entry = data.symbols[id];
                if (entry.name != name) continue;
                auto type = entry.type;
                if (tokens[tokens.size() - 2].type == TokenType::ARROW && type && type->kind == TypeKind::Pointer) type = type->base;
                if (type && (type->kind == TypeKind::Struct || type->kind == TypeKind::Union) && type->record_id < data.records.size())
                    for (const auto& field : data.records[type->record_id].members)
                        if (field.name.compare(0, prefix.size(), prefix) == 0) result.items.push_back({field.name, "成员", describe_type(field.type)});
            }
        } else {
            std::unordered_map<ScopeId, std::size_t> distances;
            auto current = scope;
            while (current != invalid_id && current < data.scopes.size() && !distances.count(current)) {
                distances[current] = distances.size(); current = data.scopes[current].parent;
            }
            for (const auto id : visible_symbols(data, prefix, scope, location)) {
                const auto& entry = data.symbols[id];
                scope_ranks[entry.name] = distances.at(entry.scope);
                result.items.push_back({entry.name, "符号", describe_type(entry.type)});
            }
        }
    }
    if (!member) {
        // 这是按编辑位置筛选的项目关键字候选，不冒充完整 LL(1) 预测表。
        bool in_block = false;
        int braces = 0;
        for (const auto& t : tokens) { if (t.type == TokenType::LBRACE) ++braces; if (t.type == TokenType::RBRACE) --braces; }
        in_block = braces > 0;
        const std::vector<std::string> types{"int", "char", "float", "double", "short", "long", "unsigned", "void", "struct", "union", "enum", "typedef", "const", "volatile", "static", "extern"};
        auto keywords = types;
        const auto last = tokens.size() > 1 ? tokens[tokens.size() - 2].type : TokenType::END_OF_FILE;
        if (in_block) keywords.insert(keywords.end(), {"if", "else", "while", "for", "do", "switch", "case", "default", "return", "break", "continue", "goto", "sizeof"});
        if (last == TokenType::ASSIGN || last == TokenType::KW_RETURN || last == TokenType::PLUS || last == TokenType::LPAREN)
            keywords = {"sizeof"};
        for (const auto& key : keywords)
            if (key.compare(0, prefix.size(), prefix) == 0 &&
                std::none_of(result.items.begin(), result.items.end(), [&](const CompletionItem& item) { return item.label == key; }))
                result.items.push_back({key, "关键字", "项目语言关键字"});
    }
    // 只保留前缀匹配；完全相同的名字优先，再按作用域由近到远排序。
    result.items.erase(std::remove_if(result.items.begin(), result.items.end(), [&](const CompletionItem& item) {
        return item.label.compare(0, prefix.size(), prefix) != 0;
    }), result.items.end());
    std::sort(result.items.begin(), result.items.end(), [&](const CompletionItem& a, const CompletionItem& b) {
        if ((a.label == prefix) != (b.label == prefix)) return a.label == prefix;
        const auto a_scope = scope_ranks.count(a.label) ? scope_ranks.at(a.label) : static_cast<std::size_t>(invalid_id);
        const auto b_scope = scope_ranks.count(b.label) ? scope_ranks.at(b.label) : static_cast<std::size_t>(invalid_id);
        if (a_scope != b_scope) return a_scope < b_scope;
        if (a.kind != b.kind) return a.kind != "关键字" && (b.kind == "关键字" || a.kind < b.kind);
        return a.label < b.label;
    });
    return result;
}
}
