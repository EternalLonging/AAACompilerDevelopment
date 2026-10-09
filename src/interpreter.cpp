#include "minic/interpreter.hpp"
#include "minic/constant_pool.hpp"
#include "minic/diagnostic.hpp"
#include "minic/semantic.hpp"
#include "internal.hpp"

#include <cctype>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace minic {
namespace {

struct Address {
    std::uint64_t frame = 0; // 0 是全局区，其余是调用帧的唯一编号。
    SymbolId symbol = invalid_id; // 被 scanf 修改的对象编号。
};
using Value = std::variant<std::monostate, std::int64_t, double, std::string, Address>;

struct Code {
    const std::vector<Quadruple>* quads = nullptr;
    const std::vector<SourceRange>* locations = nullptr;
    const std::vector<TemporaryEntry>* temporaries = nullptr;
    SymbolId function = invalid_id;
    std::unordered_map<std::string, std::size_t> labels;
    std::unordered_map<std::string, TypePtr> types;
};

struct Frame {
    const Code* code = nullptr;
    std::uint64_t id = 0;
    std::size_t pc = 0; // 下一条待执行指令。
    std::unordered_map<std::string, Value> values; // 局部对象和临时量的值。
    std::vector<Value> arguments; // 当前调用的实参队列。
    std::string destination = "-"; // 返回值写入调用者的位置。
};

std::size_t decimal(const std::string& text) {
    if (text.empty()) throw std::runtime_error("指令编号为空");
    std::size_t value = 0;
    for (const unsigned char character : text) {
        if (character < '0' || character > '9' ||
            value > (std::numeric_limits<std::size_t>::max() - (character - '0')) / 10)
            throw std::runtime_error("指令编号不是有效的十进制数");
        value = value * 10 + character - '0';
    }
    return value;
}

std::size_t operand_id(const std::string& value, char kind) {
    if (value.size() < 3 || value[0] != '%' || value[1] != kind)
        throw std::runtime_error("四元式操作数格式无效：" + value);
    return decimal(value.substr(2));
}

double number(const Value& value) {
    if (const auto* integer = std::get_if<std::int64_t>(&value)) return static_cast<double>(*integer);
    if (const auto* floating = std::get_if<double>(&value)) return *floating;
    throw std::runtime_error("指令需要数值操作数");
}

bool truth(const Value& value) { return number(value) != 0; }

Value convert_value(const Value& value, const TypePtr& target) {
    if (!target) throw std::runtime_error("目标类型为空");
    if (detail::numeric(target)) {
        const double numeric = number(value);
        if (!std::isfinite(numeric)) throw std::runtime_error("不允许非有限数值");
        if (target->kind == TypeKind::Float) return detail::checked_float(numeric);
        const double truncated = std::trunc(numeric);
        if (truncated < std::numeric_limits<std::int32_t>::min() || truncated > std::numeric_limits<std::int32_t>::max())
            throw std::runtime_error("浮点转整数或整数结果超出范围");
        return detail::checked_integer(static_cast<std::int64_t>(truncated), target->kind);
    }
    if (target->kind == TypeKind::Pointer && target->base) {
        if (std::holds_alternative<std::string>(value) && target->base->kind == TypeKind::Char) return value;
        if (std::holds_alternative<Address>(value) && detail::numeric(target->base)) return value;
    }
    throw std::runtime_error("不支持的运行值转换");
}

class Machine {
    const IRProgram& program_;
    const SymbolTableData& symbols_;
    std::istream& input_;
    std::ostream& output_;
    RunOptions options_;
    DiagnosticEngine diagnostics_{Phase::Runtime};
    std::unordered_map<SymbolId, Code> functions_;
    Code initializer_;
    std::unordered_set<SymbolId> globals_;
    std::unordered_map<std::string, Value> global_values_;
    std::vector<Frame> frames_; // 显式调用栈，递归不占用宿主 C++ 调用栈。
    std::uint64_t next_frame_ = 1;
    RunResult result_;
    SourceRange location_;

    const SymbolEntry& symbol(std::size_t id) const {
        if (id >= symbols_.symbols.size() || symbols_.symbols[id].id != id)
            throw std::runtime_error("IR 引用了无效符号编号");
        return symbols_.symbols[id];
    }

    const SymbolEntry& function(const std::string& value) const {
        const auto& entry = symbol(operand_id(value, 's'));
        if (entry.kind != SymbolKind::Function || !entry.type || entry.type->kind != TypeKind::Function || !entry.type->base)
            throw std::runtime_error("call 引用了非函数符号");
        if (entry.builtin == BuiltinKind::None && functions_.find(entry.id) == functions_.end())
            throw std::runtime_error("被调用的函数没有中间代码");
        return entry;
    }

    ScopeId function_scope(ScopeId scope) const {
        for (std::size_t count = 0; scope != invalid_id && count <= symbols_.scopes.size(); ++count) {
            if (scope >= symbols_.scopes.size()) throw std::runtime_error("符号作用域编号无效");
            if (symbols_.scopes[scope].kind == ScopeKind::Function) return scope;
            scope = symbols_.scopes[scope].parent;
        }
        return invalid_id;
    }

    TypePtr operand_type(const std::string& name, const Code& code, ScopeId& owner) const {
        if (name.size() > 2 && name[0] == '%' && name[1] == 'c') {
            const auto id = operand_id(name, 'c');
            if (id >= program_.constants.entries.size()) throw std::runtime_error("常量编号越界");
            return program_.constants.entries[id].type;
        }
        const auto temp = code.types.find(name);
        if (temp != code.types.end()) return temp->second;
        const auto& entry = symbol(operand_id(name, 's'));
        if (entry.kind != SymbolKind::Variable && entry.kind != SymbolKind::Parameter)
            throw std::runtime_error("操作数必须是数值对象或临时量");
        if (!detail::numeric(entry.type)) throw std::runtime_error("M1 对象类型不受支持");
        if (entry.scope == 0) {
            if (globals_.find(entry.id) == globals_.end()) throw std::runtime_error("全局对象没有分配存储");
        } else {
            if (code.function == invalid_id) throw std::runtime_error("全局初始化不能引用局部对象");
            const auto scope = function_scope(entry.scope);
            if (scope == invalid_id || (owner != invalid_id && owner != scope))
                throw std::runtime_error("同一函数 IR 混入了其他函数的局部对象");
            owner = scope;
        }
        return entry.type;
    }

    TypePtr target_type(const std::string& name, const Code& code, ScopeId& owner) const {
        if (name.size() > 1 && name[0] == '%' && name[1] == 'c') throw std::runtime_error("不能写入常量池");
        return operand_type(name, code, owner);
    }

    void validate_code(Code& code, const IRFunction* ir_function) {
        if (code.quads->size() != code.locations->size()) throw std::runtime_error("指令与源码位置数量不一致");
        for (const auto& entry : *code.temporaries) {
            if (entry.name != "%t" + std::to_string(code.types.size()) ||
                !(detail::numeric(entry.type) || (entry.type && entry.type->kind == TypeKind::Pointer &&
                  entry.type->base && (detail::numeric(entry.type->base)))))
                throw std::runtime_error("临时量名字、顺序或类型无效");
            code.types.emplace(entry.name, entry.type);
        }
        ScopeId owner = invalid_id;
        if (ir_function) {
            const auto& entry = symbol(ir_function->symbol_id);
            if (ir_function->parameters.size() != entry.type->params.size()) throw std::runtime_error("IR 形参数量与函数签名不一致");
            std::unordered_set<SymbolId> seen;
            for (std::size_t i = 0; i < ir_function->parameters.size(); ++i) {
                const auto& parameter = symbol(ir_function->parameters[i]);
                if (parameter.kind != SymbolKind::Parameter || !seen.insert(parameter.id).second ||
                    !same_type(parameter.type, entry.type->params[i])) throw std::runtime_error("IR 形参编号或类型无效");
                operand_type(detail::symbol_name(parameter.id), code, owner);
            }
        }
        for (std::size_t i = 0; i < code.quads->size(); ++i) {
            const auto& quad = (*code.quads)[i];
            if (quad.op == "label" && (quad.result.empty() || quad.result == "-" ||
                !code.labels.emplace(quad.result, i).second)) throw std::runtime_error("标号为空或重复");
        }
        std::vector<TypePtr> queued;
        for (std::size_t i = 0; i < code.quads->size(); ++i) {
            location_ = (*code.locations)[i];
            const auto& q = (*code.quads)[i];
            if (!queued.empty() && q.op != "arg" && q.op != "call") throw std::runtime_error("arg 指令必须紧邻本次 call");
            const auto input_type = [&](const std::string& name) { return operand_type(name, code, owner); };
            const auto destination_type = [&]() { return target_type(q.result, code, owner); };
            const auto require_numeric = [&](const std::string& name) {
                if (!detail::numeric(input_type(name))) throw std::runtime_error("操作数类型必须为数值");
            };
            if (q.op == "label" || q.op == "jmp") {
                if (q.arg1 != "-" || q.arg2 != "-" || (q.op == "jmp" && !code.labels.count(q.result)))
                    throw std::runtime_error("标号或跳转目标无效");
            } else if (q.op == "jz" || q.op == "jnz") {
                require_numeric(q.arg1);
                if (q.arg2 != "-" || !code.labels.count(q.result)) throw std::runtime_error("条件跳转目标无效");
            } else if (q.op == "local") {
                const auto& entry = symbol(operand_id(q.result, 's'));
                destination_type();
                if (entry.scope == 0 || entry.kind != SymbolKind::Variable || q.arg1 != "-" || q.arg2 != "-")
                    throw std::runtime_error("local 只能重置局部变量");
            } else if (q.op == "arg") {
                const auto argument = input_type(q.arg1);
                if (q.arg2 != "-" || q.result != "-") throw std::runtime_error("arg 格式无效");
                queued.push_back(argument);
            } else if (q.op == "call") {
                const auto& callee = function(q.arg1);
                const auto count = decimal(q.arg2);
                if (count != queued.size() || (callee.builtin == BuiltinKind::None && count != callee.type->params.size()) ||
                    (callee.builtin != BuiltinKind::None && count < 1)) throw std::runtime_error("调用的实参数量无效");
                if (callee.builtin == BuiltinKind::None) {
                    for (std::size_t argument = 0; argument < queued.size(); ++argument)
                        if (!can_assign(callee.type->params[argument], queued[argument]))
                            throw std::runtime_error("call 的实参类型与签名不兼容");
                } else if (queued.front()->kind != TypeKind::Pointer || !queued.front()->base ||
                           queued.front()->base->kind != TypeKind::Char)
                    throw std::runtime_error("输入输出的首个实参必须是字符串指针");
                if (q.result != "-" && !same_type(destination_type(), callee.type->base))
                    throw std::runtime_error("函数返回值目标类型无效");
                queued.clear();
            } else if (q.op == "ret") {
                if (code.function == invalid_id || q.arg2 != "-" || q.result != "-") throw std::runtime_error("ret 格式无效");
                const auto returned = symbol(code.function).type->base;
                if ((returned->kind == TypeKind::Void) != (q.arg1 == "-")) throw std::runtime_error("返回值与函数类型不一致");
                if (q.arg1 != "-" && !can_assign(returned, input_type(q.arg1))) throw std::runtime_error("返回值类型不兼容");
            } else if (q.op == "addr") {
                const auto& entry = symbol(operand_id(q.arg1, 's'));
                input_type(q.arg1);
                const auto target = destination_type();
                if (entry.type->is_const || target->kind != TypeKind::Pointer ||
                    !same_type(entry.type, target->base) || q.arg2 != "-") throw std::runtime_error("addr 类型无效");
            } else if (q.op == "=" || q.op == "cvt" || q.op == "cvt_i2f") {
                const auto source = input_type(q.arg1);
                const auto target = destination_type();
                if (q.arg2 != "-") throw std::runtime_error("复制或转换指令格式无效");
                if (q.op == "=" && !(same_type(source, target) || can_assign(target, source)))
                    throw std::runtime_error("复制类型不兼容");
                if (q.op == "cvt_i2f" && !(source->kind == TypeKind::Int && target->kind == TypeKind::Float))
                    throw std::runtime_error("cvt_i2f 类型无效");
                if (q.op == "cvt" && !((detail::numeric(source) && detail::numeric(target)) ||
                    (source->kind == TypeKind::Array && source->base && source->base->kind == TypeKind::Char &&
                     target->kind == TypeKind::Pointer && target->base->kind == TypeKind::Char)))
                    throw std::runtime_error("不支持该转换指令");
            } else if (q.op == "neg" || q.op == "not") {
                require_numeric(q.arg1);
                if (q.arg2 != "-" || !detail::numeric(destination_type()) ||
                    (q.op == "not" && destination_type()->kind != TypeKind::Int)) throw std::runtime_error("一元指令类型无效");
            } else if (q.op == "+" || q.op == "-" || q.op == "*" || q.op == "/" || q.op == "%" ||
                       q.op == "<" || q.op == "<=" || q.op == ">" || q.op == ">=" || q.op == "==" || q.op == "!=") {
                require_numeric(q.arg1); require_numeric(q.arg2);
                const bool comparison = q.op == "<" || q.op == "<=" || q.op == ">" || q.op == ">=" || q.op == "==" || q.op == "!=";
                if (!detail::numeric(destination_type()) || (comparison && destination_type()->kind != TypeKind::Int) ||
                    (q.op == "%" && (input_type(q.arg1)->kind == TypeKind::Float || input_type(q.arg2)->kind == TypeKind::Float)))
                    throw std::runtime_error("二元指令类型无效");
            } else throw std::runtime_error("未知指令：" + q.op);
        }
        if (!queued.empty()) throw std::runtime_error("函数末尾存在未消费的实参");
    }

    void validate() {
        for (std::size_t i = 0; i < symbols_.scopes.size(); ++i) {
            const auto& scope = symbols_.scopes[i];
            if (scope.id != i || (i == 0 ? scope.parent != invalid_id : scope.parent >= i))
                throw std::runtime_error("作用域身份或父链无效");
        }
        ConstantPool checker;
        for (std::size_t i = 0; i < program_.constants.entries.size(); ++i) {
            const auto& entry = program_.constants.entries[i];
            if (entry.id != i) throw std::runtime_error("常量池编号无效");
            checker.intern(entry.type, entry.value, entry.spelling, entry.range);
            if (detail::numeric(entry.type)) {
                if (entry.type->kind == TypeKind::Float) detail::checked_float(std::get<double>(entry.value));
                else detail::checked_integer(std::get<std::int64_t>(entry.value), entry.type->kind);
            } else if (!(entry.type->kind == TypeKind::Array && entry.type->base->kind == TypeKind::Char))
                throw std::runtime_error("M1 常量池类型不受支持");
        }
        for (const auto id : program_.globals) {
            const auto& entry = symbol(id);
            if (entry.kind != SymbolKind::Variable || entry.scope != 0 || !detail::numeric(entry.type) || !globals_.insert(id).second)
                throw std::runtime_error("全局变量清单无效");
        }
        for (const auto& function_ir : program_.functions) {
            const auto& entry = symbol(function_ir.symbol_id);
            if (entry.kind != SymbolKind::Function || !entry.is_defined || entry.builtin != BuiltinKind::None ||
                !entry.type || entry.type->kind != TypeKind::Function || !entry.type->base || entry.type->variadic ||
                !(detail::numeric(entry.type->base) || entry.type->base->kind == TypeKind::Void))
                throw std::runtime_error("函数清单含有无效签名");
            Code code;
            code.quads = &function_ir.quads;
            code.locations = &function_ir.locations;
            code.temporaries = &function_ir.temporaries;
            code.function = entry.id;
            if (!functions_.emplace(entry.id, std::move(code)).second) throw std::runtime_error("函数 IR 重复");
        }
        const auto& main = symbol(program_.entry_function);
        if (main.name != "main" || !main.type || main.type->kind != TypeKind::Function ||
            !main.type->base || main.type->base->kind != TypeKind::Int || !main.type->params.empty() ||
            main.type->variadic || !functions_.count(main.id)) throw std::runtime_error("入口必须是已定义的无参 int main");
        initializer_.quads = &program_.global_initializers;
        initializer_.locations = &program_.global_locations;
        initializer_.temporaries = &program_.global_temporaries;
        validate_code(initializer_, nullptr);
        for (const auto& function_ir : program_.functions) validate_code(functions_.at(function_ir.symbol_id), &function_ir);
    }

    TypePtr destination_type(const std::string& name, const Frame& frame) const {
        ScopeId owner = invalid_id;
        return target_type(name, *frame.code, owner);
    }

    Value read(const std::string& name, const Frame& frame) const {
        if (name.size() > 2 && name[1] == 'c') {
            const auto& constant = program_.constants.entries.at(operand_id(name, 'c'));
            if (const auto* value = std::get_if<std::int64_t>(&constant.value)) return *value;
            if (const auto* value = std::get_if<double>(&constant.value)) return *value;
            if (const auto* value = std::get_if<std::string>(&constant.value)) return *value;
            throw std::runtime_error("常量值无效");
        }
        const bool global = name.size() > 2 && name[1] == 's' && symbol(operand_id(name, 's')).scope == 0;
        const auto& values = global ? global_values_ : frame.values;
        const auto found = values.find(name);
        if (found == values.end() || std::holds_alternative<std::monostate>(found->second))
            throw std::runtime_error("读取了尚未初始化的对象或临时量：" + name);
        return found->second;
    }

    void write(const std::string& name, const Value& value, Frame& frame) {
        auto converted = convert_value(value, destination_type(name, frame));
        if (name.size() > 2 && name[1] == 's' && symbol(operand_id(name, 's')).scope == 0)
            global_values_[name] = std::move(converted);
        else frame.values[name] = std::move(converted);
    }

    void write_address(const Address& address, const Value& value, TypeKind expected) {
        const auto& entry = symbol(address.symbol);
        if (!entry.type || entry.type->kind != expected || entry.type->is_const) throw std::runtime_error("scanf 地址类型不匹配");
        const auto converted = convert_value(value, entry.type);
        if (address.frame == 0 && entry.scope == 0 && globals_.count(entry.id)) {
            global_values_[detail::symbol_name(entry.id)] = converted; return;
        }
        for (auto& frame : frames_) {
            if (frame.id == address.frame) {
                frame.values[detail::symbol_name(entry.id)] = converted;
                return;
            }
        }
        throw std::runtime_error("scanf 地址指向已经结束的调用帧");
    }

    Value builtin(const SymbolEntry& entry, const std::vector<Value>& arguments) {
        if (arguments.empty() || !std::holds_alternative<std::string>(arguments[0])) throw std::runtime_error("输入输出缺少格式字符串");
        const bool scanning = entry.builtin == BuiltinKind::Scanf;
        const auto parts = detail::format_parts(std::get<std::string>(arguments[0]), scanning);
        std::size_t conversions = 0;
        for (const auto& part : parts) if (part.conversion) ++conversions;
        if (arguments.size() != conversions + 1) throw std::runtime_error("格式串与实参数量不一致");
        std::size_t index = 1;
        std::size_t written = 0;
        for (const auto& part : parts) {
            if (!part.conversion) {
                if (scanning) {
                    for (const unsigned char character : part.text) {
                        if (std::isspace(character)) input_ >> std::ws;
                        else if (input_.get() != character) throw std::runtime_error("scanf 输入文字与格式串不匹配");
                    }
                } else { output_ << part.text; written += part.text.size(); }
                continue;
            }
            const auto& value = arguments[index++];
            if (scanning) {
                if (!std::holds_alternative<Address>(value)) throw std::runtime_error("scanf 实参必须是对象地址");
                Value input_value;
                TypeKind expected;
                if (part.conversion == 'd') {
                    std::int64_t integer = 0;
                    if (!(input_ >> integer)) throw std::runtime_error("scanf 无法读取整数");
                    input_value = detail::checked_integer(integer);
                    expected = TypeKind::Int;
                } else if (part.conversion == 'f') {
                    double floating = 0;
                    if (!(input_ >> floating)) throw std::runtime_error("scanf 无法读取浮点数");
                    input_value = detail::checked_float(floating);
                    expected = TypeKind::Float;
                } else {
                    char character = 0;
                    if (!input_.get(character)) throw std::runtime_error("scanf 无法读取字符");
                    input_value = detail::checked_integer(static_cast<unsigned char>(character), TypeKind::Char);
                    expected = TypeKind::Char;
                }
                write_address(std::get<Address>(value), input_value, expected);
            } else {
                std::ostringstream formatted;
                if (part.conversion == 's') {
                    if (!std::holds_alternative<std::string>(value)) throw std::runtime_error("%s 需要字符串");
                    const auto& text = std::get<std::string>(value);
                    formatted << text.substr(0, text.find('\0'));
                } else if (part.conversion == 'f') {
                    if (!std::holds_alternative<double>(value)) throw std::runtime_error("%f 需要 float");
                    formatted << std::fixed << std::setprecision(6) << std::get<double>(value);
                } else {
                    if (!std::holds_alternative<std::int64_t>(value)) throw std::runtime_error("%d/%c 需要整数");
                    const auto integer = std::get<std::int64_t>(value);
                    if (part.conversion == 'c') formatted << static_cast<char>(static_cast<unsigned char>(integer));
                    else formatted << integer;
                }
                output_ << formatted.str();
                written += formatted.str().size();
            }
            if (!scanning && !output_) throw std::runtime_error("程序输出失败");
        }
        if (!scanning && !output_) throw std::runtime_error("程序输出失败");
        return detail::checked_integer(static_cast<std::int64_t>(scanning ? conversions : written));
    }

    void push(SymbolId function_id, const std::vector<Value>& arguments, const std::string& destination) {
        std::size_t depth = frames_.size();
        if (!frames_.empty() && frames_.front().code->function == invalid_id) --depth;
        if (options_.max_call_depth && depth >= options_.max_call_depth) throw std::runtime_error("函数调用深度超过上限");
        if (next_frame_ == 0) throw std::runtime_error("调用帧编号已用完");
        Frame frame;
        frame.id = next_frame_++;
        frame.code = &functions_.at(function_id);
        frame.destination = destination;
        const auto& parameters = program_.functions;
        for (const auto& ir_function : parameters) {
            if (ir_function.symbol_id != function_id) continue;
            if (ir_function.parameters.size() != arguments.size()) throw std::runtime_error("实参数量不一致");
            for (std::size_t i = 0; i < arguments.size(); ++i)
                frame.values[detail::symbol_name(ir_function.parameters[i])] = convert_value(arguments[i], symbol(ir_function.parameters[i]).type);
        }
        frames_.push_back(std::move(frame));
    }

    void execute() {
        for (const auto id : program_.globals)
            global_values_[detail::symbol_name(id)] = convert_value(std::int64_t{0}, symbol(id).type);
        Frame initial;
        initial.code = &initializer_;
        frames_.push_back(std::move(initial));
        while (!frames_.empty()) {
            auto& frame = frames_.back();
            if (frame.pc >= frame.code->quads->size()) {
                if (frame.code->function != invalid_id) throw std::runtime_error("非 void 函数执行到了没有 return 的末尾");
                frames_.pop_back();
                push(program_.entry_function, {}, "-");
                continue;
            }
            location_ = (*frame.code->locations)[frame.pc];
            if (options_.max_steps && result_.executed_steps >= options_.max_steps) throw std::runtime_error("执行步数超过上限");
            ++result_.executed_steps;
            const auto q = (*frame.code->quads)[frame.pc++];
            if (q.op == "label") continue;
            if (q.op == "local") { frame.values[q.result] = std::monostate{}; continue; }
            if (q.op == "jmp" || q.op == "jz" || q.op == "jnz") {
                const bool jump = q.op == "jmp" || (q.op == "jz" ? !truth(read(q.arg1, frame)) : truth(read(q.arg1, frame)));
                if (jump) frame.pc = frame.code->labels.at(q.result);
            } else if (q.op == "=" || q.op == "cvt" || q.op == "cvt_i2f") write(q.result, read(q.arg1, frame), frame);
            else if (q.op == "addr") {
                const auto& entry = symbol(operand_id(q.arg1, 's'));
                write(q.result, Address{entry.scope == 0 ? 0 : frame.id, entry.id}, frame);
            } else if (q.op == "arg") frame.arguments.push_back(read(q.arg1, frame));
            else if (q.op == "call") {
                const auto& callee = function(q.arg1);
                auto arguments = std::move(frame.arguments);
                frame.arguments.clear();
                if (arguments.size() != decimal(q.arg2)) throw std::runtime_error("参数队列与 call 不一致");
                if (callee.builtin != BuiltinKind::None) {
                    const auto returned = builtin(callee, arguments);
                    if (q.result != "-") write(q.result, returned, frames_.back());
                } else push(callee.id, arguments, q.result);
            } else if (q.op == "ret") {
                const auto returned = q.arg1 == "-" ? Value{std::monostate{}} :
                    convert_value(read(q.arg1, frame), symbol(frame.code->function).type->base);
                const auto destination = frame.destination;
                frames_.pop_back();
                if (frames_.empty()) {
                    result_.exit_code = static_cast<std::int32_t>(std::get<std::int64_t>(returned));
                } else if (destination != "-") write(destination, returned, frames_.back());
            } else if (q.op == "neg" || q.op == "not") {
                const auto value = read(q.arg1, frame);
                write(q.result, q.op == "not" ? Value{std::int64_t{!truth(value)}} : Value{-number(value)}, frame);
            } else {
                const auto left_value = read(q.arg1, frame);
                const auto right_value = read(q.arg2, frame);
                const double left = number(left_value), right = number(right_value);
                Value value;
                if (q.op == "<") value = std::int64_t{left < right};
                else if (q.op == "<=") value = std::int64_t{left <= right};
                else if (q.op == ">") value = std::int64_t{left > right};
                else if (q.op == ">=") value = std::int64_t{left >= right};
                else if (q.op == "==") value = std::int64_t{left == right};
                else if (q.op == "!=") value = std::int64_t{left != right};
                else if (std::holds_alternative<std::int64_t>(left_value) && std::holds_alternative<std::int64_t>(right_value)) {
                    const auto a = std::get<std::int64_t>(left_value), b = std::get<std::int64_t>(right_value);
                    if ((q.op == "/" || q.op == "%") && b == 0) throw std::runtime_error("整数除零");
                    if ((q.op == "/" || q.op == "%") && a == std::numeric_limits<std::int32_t>::min() && b == -1)
                        throw std::runtime_error("整数除法溢出");
                    if (q.op == "+") value = detail::checked_integer(a + b);
                    else if (q.op == "-") value = detail::checked_integer(a - b);
                    else if (q.op == "*") value = detail::checked_integer(a * b);
                    else if (q.op == "/") value = detail::checked_integer(a / b);
                    else value = detail::checked_integer(a % b);
                } else {
                    if (q.op == "+") value = left + right;
                    else if (q.op == "-") value = left - right;
                    else if (q.op == "*") value = left * right;
                    else if (q.op == "/") {
                        if (right == 0) throw std::runtime_error("浮点除零");
                        value = left / right;
                    } else throw std::runtime_error("浮点运算符无效");
                }
                write(q.result, value, frame);
            }
        }
    }

public:
    Machine(const IRProgram& program, const SymbolTableData& symbols, std::istream& input,
            std::ostream& output, const RunOptions& options)
        : program_(program), symbols_(symbols), input_(input), output_(output), options_(options) {}

    RunResult run() {
        try {
            validate(); // 执行任何程序输出前，先检查整份 IR。
            location_ = {};
            execute();
        } catch (const std::exception& exception) {
            result_.exit_code.reset();
            diagnostics_.report(Level::Error, location_, exception.what(), "RUN_FAILURE");
        }
        result_.diagnostics = diagnostics_.take_diagnostics();
        return std::move(result_);
    }
};

}

RunResult run(const IRProgram& program, const SymbolTableData& symbols,
              std::istream& input, std::ostream& output, const RunOptions& options) {
    return Machine(program, symbols, input, output, options).run();
}

}
