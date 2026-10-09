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
    static std::int64_t checked(std::int64_t value) {
        if (value < -2147483648LL || value > 2147483647LL) throw std::runtime_error("条件整数超出有符号 32 位范围");
        return value;
    }
    static std::int64_t signed_bits(std::uint32_t value) { return value > 2147483647U ? static_cast<std::int64_t>(value) - 4294967296LL : value; }
    std::int64_t unary(bool evaluate) {
        if (++depth_ > 128) throw std::runtime_error("条件表达式嵌套过深");
        struct Exit { std::size_t& depth; ~Exit() { --depth; } } exit{depth_};
        if (take("!")) return !unary(evaluate);
        if (take("~")) { const auto value = unary(evaluate); return evaluate ? signed_bits(~static_cast<std::uint32_t>(value)) : 0; }
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
    std::int64_t expression(int precedence, bool evaluate) {
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
                if (token == "<<") { if (left < 0) throw std::runtime_error("条件负数不能左移"); left = checked(left * (std::int64_t{1} << right)); }
                else { const auto divisor = std::int64_t{1} << right; left = left >= 0 ? left / divisor : -((-left + divisor - 1) / divisor); }
            } else {
                const auto a = static_cast<std::uint32_t>(left), b = static_cast<std::uint32_t>(right);
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
    IncludeLoader loader_; // 调用者提供的头文件读取接口。
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

    std::string expand(const std::string& text, std::unordered_set<std::string> disabled = {}, std::size_t depth = 0) {
        if (text.size() > 16 * 1024 * 1024) throw std::runtime_error("宏展开输入过大");
        if (depth > 128 || expansions_ > 100000) throw std::runtime_error("宏展开超过数量或深度限制");
        std::string output;
        for (std::size_t i = 0; i < text.size();) {
            if (text[i] == '"' || text[i] == '\'') { const auto end = quoted_end(text, i); output += text.substr(i, end - i); i = end; continue; }
            if (std::isdigit(static_cast<unsigned char>(text[i])) || (text[i] == '.' && i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1])))) {
                const auto start = i++;
                while (i < text.size() && (name_part(static_cast<unsigned char>(text[i])) || text[i] == '.' ||
                    ((text[i] == '+' || text[i] == '-') && (text[i - 1] == 'e' || text[i - 1] == 'E' || text[i - 1] == 'p' || text[i - 1] == 'P')))) ++i;
                output += text.substr(start, i - start); continue;
            }
            if (!name_start(static_cast<unsigned char>(text[i]))) { output += text[i++]; continue; }
            const auto start = i++; while (i < text.size() && name_part(static_cast<unsigned char>(text[i]))) ++i;
            const auto name = text.substr(start, i - start);
            const auto found = macros_.find(name);
            if (found == macros_.end() || disabled.count(name)) { output += name; continue; }
            const auto macro = found->second;
            std::string replacement = macro.body;
            if (macro.function) {
                auto open = i; while (open < text.size() && std::isspace(static_cast<unsigned char>(text[open]))) ++open;
                if (open == text.size() || text[open] != '(') { output += name; continue; }
                std::vector<std::string> arguments; std::size_t begin = open + 1, nesting = 0, end = begin;
                for (; end < text.size(); ++end) {
                    if (text[end] == '"' || text[end] == '\'') { end = quoted_end(text, end) - 1; continue; }
                    if (text[end] == '(') ++nesting;
                    else if (text[end] == ')' && nesting) --nesting;
                    else if (text[end] == ')' || (text[end] == ',' && !nesting)) {
                        arguments.push_back(trim(text.substr(begin, end - begin))); begin = end + 1;
                        if (text[end] == ')') break;
                    }
                }
                if (end == text.size()) throw std::runtime_error("函数宏调用缺少右括号");
                if (arguments.size() == 1 && arguments[0].empty() && macro.parameters.empty()) arguments.clear();
                if (arguments.size() != macro.parameters.size()) throw std::runtime_error("函数宏实参数量不匹配：" + name);
                std::unordered_map<std::string, std::string> substitutions;
                for (std::size_t a = 0; a < arguments.size(); ++a) substitutions[macro.parameters[a]] = expand(arguments[a], disabled, depth + 1);
                replacement.clear();
                for (std::size_t p = 0; p < macro.body.size();) {
                    if (macro.body[p] == '"' || macro.body[p] == '\'') { const auto q = quoted_end(macro.body, p); replacement += macro.body.substr(p, q - p); p = q; continue; }
                    if (!name_start(static_cast<unsigned char>(macro.body[p]))) { replacement += macro.body[p++]; continue; }
                    const auto from = p++; while (p < macro.body.size() && name_part(static_cast<unsigned char>(macro.body[p]))) ++p;
                    const auto token = macro.body.substr(from, p - from);
                    const auto argument = substitutions.find(token);
                    replacement += argument == substitutions.end() ? token : argument->second;
                }
                i = end + 1;
            }
            ++expansions_; auto nested = disabled; nested.insert(name);
            output += expand(replacement, std::move(nested), depth + 1);
            if (output.size() > 16 * 1024 * 1024) throw std::runtime_error("宏展开结果过大");
        }
        return output;
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
        for (std::size_t i = 0; i < macro.body.size(); ++i) {
            if (macro.body[i] == '"' || macro.body[i] == '\'') i = quoted_end(macro.body, i) - 1;
            else if (macro.body[i] == '#') throw std::runtime_error("暂不支持宏字符串化和 token 拼接");
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

    void emit(const std::string& line, const std::string& file, std::uint32_t number) {
        if (result_.source.size() + line.size() > 16 * 1024 * 1024) throw std::runtime_error("预处理结果超过 16 MiB");
        result_.source += line + '\n'; result_.lines.push_back({file, number});
    }

    void file(const std::string& source, const std::string& filename, std::size_t depth) {
        if (depth > 64) throw std::runtime_error("头文件包含超过 64 层");
        if (source.size() > 16 * 1024 * 1024) throw std::runtime_error("单个输入文件超过 16 MiB");
        std::istringstream input(source); std::string line; std::uint32_t number = 0;
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
                const auto path = trim(expand(argument));
                if (path.size() < 3 || !((path.front() == '"' && path.back() == '"') || (path.front() == '<' && path.back() == '>')))
                    throw std::runtime_error("include 需要引号或尖括号头文件名");
                if (!loader_) throw std::runtime_error("尚未提供头文件读取接口");
                const auto header = path.substr(1, path.size() - 2); const auto contents = loader_(header, filename);
                if (!contents) throw std::runtime_error("找不到头文件：" + header);
                file(*contents, header, depth + 1);
                location_.file = filename; location_.begin.line = first; location_.end.line = number;
            } else if (active && name == "error") throw std::runtime_error("error 指令：" + argument);
            else if (active && !name.empty()) throw std::runtime_error("暂不支持预处理指令：" + name);
            emit("", filename, first);
        }
        if (block) throw std::runtime_error("块注释没有结束");
        if (!stack.empty()) throw std::runtime_error("条件指令缺少 endif");
    }
public:
    explicit Processor(IncludeLoader loader) : loader_(std::move(loader)) {}
    PreprocessResult run(const std::string& source, const std::string& filename) {
        try { file(source, filename, 0); }
        catch (const std::exception& error) { diagnostics_.report(Level::Error, location_, error.what(), "PP_FAILURE"); }
        result_.diagnostics = diagnostics_.take_diagnostics(); return std::move(result_);
    }
};
}
PreprocessResult preprocess(const std::string& source, const std::string& filename, const IncludeLoader& loader) {
    return Processor(loader).run(source, filename);
}
}
