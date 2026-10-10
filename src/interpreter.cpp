#include "minic/interpreter.hpp"
#include "minic/constant_pool.hpp"
#include "minic/diagnostic.hpp"
#include "minic/semantic.hpp"
#include "internal.hpp"

#include <cctype>
#include <cstring>
#include <unordered_set>
#include <iomanip>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace minic {
namespace {

struct Address {
    std::uint64_t frame = 0; // 0 是全局区，其余是调用帧的唯一编号。
    SymbolId symbol = invalid_id; // 所指对象编号，invalid_id 表示空指针。
    std::size_t offset = 0; // 子对象相对根对象的字节偏移。
    TypePtr type = nullptr; // 当前地址所指向的子对象类型。
    std::size_t first = 0; // 可移动范围的起点，包含此偏移。
    std::size_t last = 0; // 可移动范围的终点，仅允许形成尾后指针。
    std::shared_ptr<const std::string> literal; // 指向常量字符串时保存只读字节。
    std::string temporary; // 临时聚合对象的名字，普通地址为空。
};
struct Aggregate;
using Value = std::variant<std::monostate, std::int64_t, double, std::string, Address, std::shared_ptr<Aggregate>>;
struct Aggregate {
    std::unordered_map<std::size_t, Value> cells; // 按字节偏移保存已写入的数值叶子。
    std::unordered_map<std::size_t, unsigned char> bytes; // 数值对象的小端字节，联合体成员共享这些字节。
    std::unordered_set<std::size_t> unknown; // 默认零对象中仍未初始化的字节。
    bool zero = false; // 未写入的叶子是否已零初始化。
};

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

bool truth(const Value& value) {
    if (const auto* address = std::get_if<Address>(&value)) return address->symbol != invalid_id || address->literal || !address->temporary.empty();
    if (std::holds_alternative<std::string>(value)) return true;
    return number(value) != 0;
}

bool assign_type(const TypePtr& target, const TypePtr& source) {
    return same_type(target, source) || can_assign(target, source) || detail::pointer_assign(target, source) ||
        (target && source && detail::aggregate(target) && detail::same_unqualified(target, source));
}

bool compatible_subobject(const TypePtr& target, const TypePtr& member, bool parent_const, bool parent_volatile) {
    if (!target || !member) return false;
    auto qualified = *member;
    qualified.is_const = qualified.is_const || parent_const;
    qualified.is_volatile = qualified.is_volatile || parent_volatile;
    return same_type(target, &qualified);
}

Value convert_value(const Value& value, const TypePtr& target) {
    if (!target) throw std::runtime_error("目标类型为空");
    if (detail::numeric(target)) {
        const double numeric = number(value);
        if (!std::isfinite(numeric)) throw std::runtime_error("不允许非有限数值");
        if (detail::floating(target)) return target->kind == TypeKind::Float ? detail::checked_float(numeric) : numeric;
        const double truncated = std::trunc(numeric);
        const auto bits = detail::integer_bits(target);
        const auto modulus = std::uint64_t{1} << bits;
        if (target->is_unsigned) {
            if (std::holds_alternative<std::int64_t>(value)) return static_cast<std::int64_t>(static_cast<std::uint64_t>(std::get<std::int64_t>(value)) % modulus);
            if (truncated < 0 || truncated >= static_cast<double>(modulus)) throw std::runtime_error("浮点转无符号整数超出范围");
            return static_cast<std::int64_t>(truncated);
        }
        const auto low = -(std::int64_t{1} << (bits - 1)), high = (std::int64_t{1} << (bits - 1)) - 1;
        if (truncated < static_cast<double>(low) || truncated > static_cast<double>(high))
            throw std::runtime_error("浮点转整数或整数结果超出范围");
        return static_cast<std::int64_t>(truncated);
    }
    if (target->kind == TypeKind::Pointer && target->base) {
        if (const auto* text = std::get_if<std::string>(&value); text && target->base->kind == TypeKind::Char)
            return Address{0, invalid_id, 0, target->base, 0, text->size() + 1, std::make_shared<const std::string>(*text), {}};
        if (const auto* address = std::get_if<Address>(&value)) {
            auto converted = *address; converted.type = target->base; return converted;
        }
        if (const auto* integer = std::get_if<std::int64_t>(&value); integer && *integer == 0)
            return Address{0, invalid_id, 0, target->base, 0, 0, {}, {}};
    }
    if (detail::aggregate(target) && std::holds_alternative<std::shared_ptr<Aggregate>>(value))
        return std::make_shared<Aggregate>(*std::get<std::shared_ptr<Aggregate>>(value));
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
    std::unordered_map<std::string, std::shared_ptr<const std::string>> strings_; // 同内容字面量复用同一个只读对象。
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
        if (entry.kind != SymbolKind::Variable && entry.kind != SymbolKind::Parameter && entry.kind != SymbolKind::Array)
            throw std::runtime_error("操作数必须是数值对象或临时量");
        detail::layout(entry.type, symbols_);
        if (entry.scope == 0 || entry.storage == StorageClass::Static) {
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
                !(detail::numeric(entry.type) || detail::aggregate(entry.type) ||
                  (entry.type && entry.type->kind == TypeKind::Pointer && entry.type->base)))
                throw std::runtime_error("临时量名字、顺序或类型无效");
            if (detail::aggregate(entry.type)) detail::layout(entry.type, symbols_);
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
                    !same_type(detail::unqualified(parameter.type), entry.type->params[i])) throw std::runtime_error("IR 形参编号或类型无效");
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
            if (!queued.empty() && q.op != "arg" && q.op != "call" && q.op != "callind") throw std::runtime_error("arg 指令必须紧邻本次 call");
            const auto input_type = [&](const std::string& name) { return operand_type(name, code, owner); };
            const auto destination_type = [&]() { return target_type(q.result, code, owner); };
            const auto require_numeric = [&](const std::string& name) {
                if (!detail::numeric(input_type(name))) throw std::runtime_error("操作数类型必须为数值");
            };
            const auto require_scalar = [&](const std::string& name) {
                const auto type = input_type(name);
                if (!detail::numeric(type) && type->kind != TypeKind::Pointer)
                    throw std::runtime_error("操作数类型必须为数值或指针");
            };
            if (q.op == "label" || q.op == "jmp") {
                if (q.arg1 != "-" || q.arg2 != "-" || (q.op == "jmp" && !code.labels.count(q.result)))
                    throw std::runtime_error("标号或跳转目标无效");
            } else if (q.op == "jz" || q.op == "jnz") {
                require_scalar(q.arg1);
                if (q.arg2 != "-" || !code.labels.count(q.result)) throw std::runtime_error("条件跳转目标无效");
            } else if (q.op == "local" || q.op == "zero") {
                const auto& entry = symbol(operand_id(q.result, 's'));
                destination_type();
                if ((q.op == "local" && globals_.count(entry.id)) ||
                    (entry.kind != SymbolKind::Variable && entry.kind != SymbolKind::Array) || q.arg1 != "-" || q.arg2 != "-")
                    throw std::runtime_error("local 只能重置局部变量");
            } else if (q.op == "load" || q.op == "store") {
                const auto pointer = input_type(q.op == "load" ? q.arg1 : q.result);
                if (!pointer || pointer->kind != TypeKind::Pointer || !pointer->base || q.arg2 != "-")
                    throw std::runtime_error("load/store 需要合法对象地址");
                const auto value = q.op == "load" ? destination_type() : input_type(q.arg1);
                if (!assign_type(q.op == "load" ? value : pointer->base, q.op == "load" ? pointer->base : value))
                    throw std::runtime_error("load/store 的值与所指类型不兼容");
            } else if (q.op == "indexaddr") {
                const auto base = input_type(q.arg1), target = destination_type();
                const auto index = input_type(q.arg2);
                if (base->kind != TypeKind::Pointer || !base->base || base->base->kind != TypeKind::Array ||
                    target->kind != TypeKind::Pointer || !detail::numeric(index) || detail::floating(index) ||
                    !compatible_subobject(target->base, base->base->base, base->base->is_const, base->base->is_volatile))
                    throw std::runtime_error("indexaddr 类型无效");
            } else if (q.op == "decay") {
                const auto base = input_type(q.arg1), target = destination_type();
                if (q.arg2 != "-" || base->kind != TypeKind::Pointer || !base->base || base->base->kind != TypeKind::Array ||
                    target->kind != TypeKind::Pointer || !compatible_subobject(target->base, base->base->base, base->base->is_const, base->base->is_volatile))
                    throw std::runtime_error("decay 类型无效");
            } else if (q.op == "ptradd" || q.op == "ptrsub") {
                const auto base = input_type(q.arg1), index = input_type(q.arg2), target = destination_type();
                if (base->kind != TypeKind::Pointer || !base->base || !detail::pointer_assign(target, base) ||
                    !detail::numeric(index) || detail::floating(index)) throw std::runtime_error("指针偏移类型无效");
                detail::layout(base->base, symbols_);
            } else if (q.op == "ptrdiff") {
                const auto left = input_type(q.arg1), right = input_type(q.arg2);
                if (!(detail::pointer_assign(left, right) || detail::pointer_assign(right, left)) || destination_type()->kind != TypeKind::Int)
                    throw std::runtime_error("指针差值类型无效");
                detail::layout(left->base, symbols_);
            } else if (q.op == "memberaddr") {
                const auto base = input_type(q.arg1), target = destination_type();
                if (base->kind != TypeKind::Pointer || !base->base || (base->base->kind != TypeKind::Struct && base->base->kind != TypeKind::Union) ||
                    target->kind != TypeKind::Pointer || !target->base)
                    throw std::runtime_error("memberaddr 类型无效");
                const auto offset = decimal(q.arg2);
                bool found = false;
                for (const auto& member : symbols_.records.at(base->base->record_id).members)
                    if (member.offset == offset && compatible_subobject(target->base, member.type, base->base->is_const, base->base->is_volatile)) found = true;
                if (!found) throw std::runtime_error("memberaddr 偏移或类型不对应已定义的成员");
            } else if (q.op == "arg") {
                const auto argument = input_type(q.arg1);
                if (q.arg2 != "-" || q.result != "-") throw std::runtime_error("arg 格式无效");
                queued.push_back(argument);
            } else if (q.op == "call" || q.op == "callind") {
                SymbolEntry callee;
                if (q.op == "call") callee = function(q.arg1);
                else {
                    const auto pointer = input_type(q.arg1);
                    if (pointer->kind != TypeKind::Pointer || !pointer->base || pointer->base->kind != TypeKind::Function || pointer->base->variadic)
                        throw std::runtime_error("间接调用需要固定签名的函数指针");
                    callee.type = pointer->base;
                }
                const auto count = decimal(q.arg2);
                if (count != queued.size() || (callee.builtin == BuiltinKind::None && count != callee.type->params.size()) ||
                    (callee.builtin != BuiltinKind::None && count < 1)) throw std::runtime_error("调用的实参数量无效");
                if (callee.builtin == BuiltinKind::None) {
                    for (std::size_t argument = 0; argument < queued.size(); ++argument)
                        if (!assign_type(callee.type->params[argument], queued[argument]))
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
                if (q.arg1 != "-" && !assign_type(returned, input_type(q.arg1))) throw std::runtime_error("返回值类型不兼容");
            } else if (q.op == "faddr") {
                const auto& entry = function(q.arg1); const auto target = destination_type();
                if (q.arg2 != "-" || target->kind != TypeKind::Pointer || !same_type(target->base, entry.type)) throw std::runtime_error("函数地址类型无效");
            } else if (q.op == "tempaddr") {
                const auto source = input_type(q.arg1), target = destination_type();
                if (q.arg2 != "-" || !code.types.count(q.arg1) || !detail::aggregate(source) || target->kind != TypeKind::Pointer || !same_type(source, target->base))
                    throw std::runtime_error("临时聚合地址类型无效");
            } else if (q.op == "addr") {
                const auto& entry = symbol(operand_id(q.arg1, 's'));
                input_type(q.arg1);
                const auto target = destination_type();
                if (target->kind != TypeKind::Pointer ||
                    !same_type(entry.type, target->base) || q.arg2 != "-") throw std::runtime_error("addr 类型无效");
            } else if (q.op == "=" || q.op == "cvt" || q.op == "cvt_i2f") {
                const auto source = input_type(q.arg1);
                const auto target = destination_type();
                if (q.arg2 != "-") throw std::runtime_error("复制或转换指令格式无效");
                if (q.op == "=" && !assign_type(target, source))
                    throw std::runtime_error("复制类型不兼容");
                if (q.op == "cvt_i2f" && !(source->kind == TypeKind::Int && target->kind == TypeKind::Float))
                    throw std::runtime_error("cvt_i2f 类型无效");
                if (q.op == "cvt" && !((detail::numeric(source) && detail::numeric(target)) ||
                    detail::pointer_assign(target, source) ||
                    (target->kind == TypeKind::Pointer && source->kind == TypeKind::Pointer && target->base && source->base &&
                     target->base->kind != TypeKind::Function && source->base->kind != TypeKind::Function &&
                     (target->base->kind == TypeKind::Char || source->base->kind == TypeKind::Char) &&
                     (!source->base->is_const || target->base->is_const) && (!source->base->is_volatile || target->base->is_volatile)) ||
                    (target->kind == TypeKind::Pointer && detail::numeric(source) && !detail::floating(source) &&
                     q.arg1.size() > 2 && q.arg1[1] == 'c' &&
                     std::get<std::int64_t>(program_.constants.entries.at(operand_id(q.arg1, 'c')).value) == 0) ||
                    (source->kind == TypeKind::Array && source->base && source->base->kind == TypeKind::Char &&
                     target->kind == TypeKind::Pointer && target->base->kind == TypeKind::Char)))
                    throw std::runtime_error("不支持该转换指令");
            } else if (q.op == "neg" || q.op == "not" || q.op == "bnot") {
                if (q.op == "not") require_scalar(q.arg1); else require_numeric(q.arg1);
                if (q.arg2 != "-" || !detail::numeric(destination_type()) ||
                    (q.op == "not" && destination_type()->kind != TypeKind::Int) ||
                    (q.op == "bnot" && (!detail::integral(input_type(q.arg1)) || !detail::integral(destination_type())))) throw std::runtime_error("一元指令类型无效");
            } else if (q.op == "+" || q.op == "-" || q.op == "*" || q.op == "/" || q.op == "%" ||
                       q.op == "<" || q.op == "<=" || q.op == ">" || q.op == ">=" || q.op == "==" || q.op == "!=" ||
                       q.op == "&" || q.op == "|" || q.op == "^" || q.op == "<<" || q.op == ">>") {
                const auto left = input_type(q.arg1), right = input_type(q.arg2);
                const bool pointer_equality = (q.op == "==" || q.op == "!=" || q.op == "<" || q.op == "<=" || q.op == ">" || q.op == ">=") &&
                    (detail::pointer_assign(left, right) || detail::pointer_assign(right, left));
                if (!pointer_equality) { require_numeric(q.arg1); require_numeric(q.arg2); }
                const bool comparison = q.op == "<" || q.op == "<=" || q.op == ">" || q.op == ">=" || q.op == "==" || q.op == "!=";
                const bool integral = q.op == "%" || q.op == "&" || q.op == "|" || q.op == "^" || q.op == "<<" || q.op == ">>";
                if (!detail::numeric(destination_type()) || (comparison && destination_type()->kind != TypeKind::Int) ||
                    (integral && (detail::floating(input_type(q.arg1)) || detail::floating(input_type(q.arg2)) || detail::floating(destination_type()))))
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
                if (detail::floating(entry.type)) convert_value(std::get<double>(entry.value), entry.type);
                else if (entry.type->is_unsigned) {
                    const auto value = std::get<std::uint64_t>(entry.value);
                    if (value >= (std::uint64_t{1} << detail::integer_bits(entry.type))) throw std::runtime_error("无符号常量超出范围");
                } else convert_value(std::get<std::int64_t>(entry.value), entry.type);
            } else if (!(entry.type->kind == TypeKind::Array && entry.type->base->kind == TypeKind::Char))
                throw std::runtime_error("M1 常量池类型不受支持");
        }
        for (const auto id : program_.globals) {
            const auto& entry = symbol(id);
            if ((entry.kind != SymbolKind::Variable && entry.kind != SymbolKind::Array) ||
                (entry.scope != 0 && entry.storage != StorageClass::Static) || !globals_.insert(id).second)
                throw std::runtime_error("全局变量清单无效");
            detail::layout(entry.type, symbols_);
        }
        for (const auto& function_ir : program_.functions) {
            const auto& entry = symbol(function_ir.symbol_id);
            if (entry.kind != SymbolKind::Function || !entry.is_defined || entry.builtin != BuiltinKind::None ||
                !entry.type || entry.type->kind != TypeKind::Function || !entry.type->base || entry.type->variadic ||
                !(detail::numeric(entry.type->base) || entry.type->base->kind == TypeKind::Void || entry.type->base->kind == TypeKind::Pointer || detail::aggregate(entry.type->base)))
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
            if (const auto* value = std::get_if<std::uint64_t>(&constant.value)) return static_cast<std::int64_t>(*value);
            if (const auto* value = std::get_if<double>(&constant.value)) return *value;
            if (const auto* value = std::get_if<std::string>(&constant.value)) return *value;
            throw std::runtime_error("常量值无效");
        }
        const bool global = name.size() > 2 && name[1] == 's' && globals_.count(static_cast<SymbolId>(operand_id(name, 's')));
        const auto& values = global ? global_values_ : frame.values;
        const auto found = values.find(name);
        if (found == values.end() || std::holds_alternative<std::monostate>(found->second))
            throw std::runtime_error("读取了尚未初始化的对象或临时量：" + name);
        return found->second;
    }

    Value initial_value(const TypePtr& type, bool zero) const {
        if (detail::aggregate(type)) {
            auto object = std::make_shared<Aggregate>(); object->zero = zero; return object;
        }
        return zero ? convert_value(std::int64_t{0}, type) : Value{std::monostate{}};
    }

    Value& root_value(const Address& address) {
        if (!address.temporary.empty()) {
            for (auto& frame : frames_) if (frame.id == address.frame) {
                const auto total = detail::layout(frame.code->types.at(address.temporary), symbols_).size;
                const auto size = detail::layout(address.type, symbols_).size;
                if (address.last > total || address.offset < address.first || address.offset > address.last || size > address.last - address.offset)
                    throw std::runtime_error("临时对象地址越界");
                return frame.values.at(address.temporary);
            }
            throw std::runtime_error("临时对象所属调用帧已结束");
        }
        if (address.symbol == invalid_id) throw std::runtime_error("不能解引用空指针");
        const auto& entry = symbol(address.symbol);
        if (!address.type) throw std::runtime_error("地址缺少所指类型");
        const auto total = detail::layout(entry.type, symbols_).size;
        const auto selected = detail::layout(address.type, symbols_).size;
        if (address.offset % detail::layout(address.type, symbols_).alignment) throw std::runtime_error("指针所指地址不满足类型对齐");
        if (address.last > total || address.offset < address.first || address.offset > address.last || selected > address.last - address.offset)
            throw std::runtime_error("地址超出所属对象边界，尾后指针不能解引用");
        if (address.frame == 0 && globals_.count(entry.id))
            return global_values_.at(detail::symbol_name(entry.id));
        for (auto& frame : frames_) {
            if (frame.id != address.frame) continue;
            const auto found = frame.values.find(detail::symbol_name(entry.id));
            if (found == frame.values.end()) throw std::runtime_error("目标对象的声明尚未执行");
            return found->second;
        }
        throw std::runtime_error("地址指向已经结束的调用帧");
    }

    Value read_address(const Address& address) {
        if (address.literal) {
            if (!address.type || address.type->kind != TypeKind::Char || address.offset >= address.last)
                throw std::runtime_error("字符串地址越界或类型无效");
            const auto byte = address.offset == address.literal->size() ? 0 : static_cast<unsigned char>((*address.literal)[address.offset]);
            return static_cast<std::int64_t>(byte > 127 ? byte - 256 : byte);
        }
        auto& root = root_value(address);
        TypeInfo temporary_root; temporary_root.kind = TypeKind::Struct;
        const auto root_type = address.temporary.empty() ? symbol(address.symbol).type : &temporary_root;
        if (!detail::aggregate(root_type)) {
            if (std::holds_alternative<std::monostate>(root)) throw std::runtime_error("读取尚未初始化的变量");
            if (!detail::same_unqualified(root_type, address.type)) {
                if (address.type->kind != TypeKind::Char || !detail::numeric(root_type)) throw std::runtime_error("指针所指类型与根对象不兼容");
                std::uint64_t bits;
                if (detail::integral(root_type)) bits = static_cast<std::uint64_t>(std::get<std::int64_t>(root));
                else if (root_type->kind == TypeKind::Float) { const auto value = static_cast<float>(std::get<double>(root)); std::uint32_t word; std::memcpy(&word, &value, 4); bits = word; }
                else { const auto value = std::get<double>(root); std::memcpy(&bits, &value, 8); }
                const auto byte = static_cast<std::int64_t>((bits >> (8 * address.offset)) & 255);
                return address.type->is_unsigned || byte < 128 ? byte : byte - 256;
            }
            return root;
        }
        const auto object = std::get<std::shared_ptr<Aggregate>>(root);
        if (detail::aggregate(address.type)) {
            auto result = std::make_shared<Aggregate>(); result->zero = object->zero;
            const auto size = detail::layout(address.type, symbols_).size;
            for (const auto& cell : object->cells)
                if (cell.first >= address.offset && cell.first - address.offset < size)
                    result->cells.emplace(cell.first - address.offset, cell.second);
            for (const auto& byte : object->bytes)
                if (byte.first >= address.offset && byte.first - address.offset < size) result->bytes.emplace(byte.first - address.offset, byte.second);
            for (const auto offset : object->unknown)
                if (offset >= address.offset && offset - address.offset < size) result->unknown.insert(offset - address.offset);
            return result;
        }
        if (detail::numeric(address.type)) {
            const auto size = detail::layout(address.type, symbols_).size;
            for (const auto& cell : object->cells)
                if (cell.first < address.offset + size && cell.first + 8 > address.offset)
                    throw std::runtime_error("不能把指针表示读取为数值");
            std::uint64_t bits = 0;
            for (std::size_t i = 0; i < size; ++i) {
                const auto byte = object->bytes.find(address.offset + i);
                if (object->unknown.count(address.offset + i) || (byte == object->bytes.end() && !object->zero)) throw std::runtime_error("读取尚未初始化的数值字节");
                if (object->cells.count(address.offset + i)) throw std::runtime_error("不能把指针表示读取为数值");
                bits |= static_cast<std::uint64_t>(byte == object->bytes.end() ? 0 : byte->second) << (8 * i);
            }
            if (detail::floating(address.type)) {
                if (size == 4) { const auto word = static_cast<std::uint32_t>(bits); float value; std::memcpy(&value, &word, 4); return convert_value(static_cast<double>(value), address.type); }
                double value; std::memcpy(&value, &bits, 8); return convert_value(value, address.type);
            }
            if (!address.type->is_unsigned && (bits & (std::uint64_t{1} << (size * 8 - 1)))) return static_cast<std::int64_t>(bits) - static_cast<std::int64_t>(std::uint64_t{1} << (size * 8));
            return static_cast<std::int64_t>(bits);
        }
        const auto cell = object->cells.find(address.offset);
        if (cell != object->cells.end()) {
            if (std::holds_alternative<std::monostate>(cell->second)) throw std::runtime_error("读取尚未初始化的聚合子对象");
            return cell->second;
        }
        for (std::size_t i = 0; i < detail::layout(address.type, symbols_).size; ++i)
            if (object->bytes.count(address.offset + i) || object->unknown.count(address.offset + i)) throw std::runtime_error("不能把数值表示读取为指针");
        if (!object->zero) throw std::runtime_error("读取尚未初始化的数组元素或结构体成员");
        return convert_value(std::int64_t{0}, address.type);
    }

    void store_address(const Address& address, const Value& value) {
        if (address.literal) throw std::runtime_error("不能修改字符串字面量");
        auto converted = convert_value(value, address.type);
        auto& root = root_value(address);
        if (address.temporary.empty() && !detail::aggregate(symbol(address.symbol).type)) {
            const auto root_type = symbol(address.symbol).type;
            if (!detail::same_unqualified(root_type, address.type)) {
                if (address.type->kind != TypeKind::Char || !detail::numeric(root_type) || std::holds_alternative<std::monostate>(root))
                    throw std::runtime_error("指针写入类型不兼容或原对象未初始化");
                std::uint64_t bits;
                if (detail::integral(root_type)) bits = static_cast<std::uint64_t>(std::get<std::int64_t>(root));
                else if (root_type->kind == TypeKind::Float) { const auto value = static_cast<float>(std::get<double>(root)); std::uint32_t word; std::memcpy(&word, &value, 4); bits = word; }
                else { const auto value = std::get<double>(root); std::memcpy(&bits, &value, 8); }
                bits = (bits & ~(std::uint64_t{255} << (8 * address.offset))) | ((static_cast<std::uint64_t>(std::get<std::int64_t>(converted)) & 255) << (8 * address.offset));
                if (detail::floating(root_type)) {
                    if (root_type->kind == TypeKind::Float) { const auto word = static_cast<std::uint32_t>(bits); float value; std::memcpy(&value, &word, 4); root = convert_value(static_cast<double>(value), root_type); }
                    else { double value; std::memcpy(&value, &bits, 8); root = convert_value(value, root_type); }
                } else {
                    const auto size = detail::integer_bits(root_type);
                    bits &= (std::uint64_t{1} << size) - 1;
                    root = !root_type->is_unsigned && (bits & (std::uint64_t{1} << (size - 1))) ?
                        Value{static_cast<std::int64_t>(bits) - static_cast<std::int64_t>(std::uint64_t{1} << size)} : Value{static_cast<std::int64_t>(bits)};
                }
                return;
            }
            root = std::move(converted); return;
        }
        auto object = std::get<std::shared_ptr<Aggregate>>(root);
        if (!detail::aggregate(address.type)) {
            const auto size = detail::layout(address.type, symbols_).size;
            for (auto it = object->cells.begin(); it != object->cells.end();) {
                if (it->first < address.offset + size && it->first + 8 > address.offset) it = object->cells.erase(it); else ++it;
            }
            for (std::size_t i = 0; i < size; ++i) { object->bytes.erase(address.offset + i); object->unknown.erase(address.offset + i); }
            if (detail::numeric(address.type)) {
                std::uint64_t bits;
                if (detail::integral(address.type)) bits = static_cast<std::uint64_t>(std::get<std::int64_t>(converted));
                else if (size == 4) { const auto value = static_cast<float>(std::get<double>(converted)); std::uint32_t word; std::memcpy(&word, &value, 4); bits = word; }
                else { const auto value = std::get<double>(converted); std::memcpy(&bits, &value, 8); }
                for (std::size_t i = 0; i < size; ++i) object->bytes[address.offset + i] = static_cast<unsigned char>(bits >> (8 * i));
            } else object->cells[address.offset] = std::move(converted);
            return;
        }
        const auto size = detail::layout(address.type, symbols_).size;
        auto source = std::get<std::shared_ptr<Aggregate>>(converted);
        for (auto it = object->cells.begin(); it != object->cells.end();) {
            if (it->first >= address.offset && it->first - address.offset < size) it = object->cells.erase(it);
            else ++it;
        }
        for (auto it = object->bytes.begin(); it != object->bytes.end();) {
            if (it->first >= address.offset && it->first - address.offset < size) it = object->bytes.erase(it); else ++it;
        }
        for (auto it = object->unknown.begin(); it != object->unknown.end();) {
            if (*it >= address.offset && *it - address.offset < size) it = object->unknown.erase(it); else ++it;
        }
        if (address.offset == 0 && address.temporary.empty() && same_type(address.type, symbol(address.symbol).type)) object->zero = source->zero;
        else if (source->zero != object->zero) materialize_default(*source, address.type, 0);
        for (const auto& cell : source->cells) object->cells[address.offset + cell.first] = cell.second;
        for (const auto& byte : source->bytes) object->bytes[address.offset + byte.first] = byte.second;
        for (const auto offset : source->unknown) object->unknown.insert(address.offset + offset);
    }

    // 子聚合的默认状态与父对象不同时，显式保留每个数值叶子的状态。
    void materialize_default(Aggregate& object, const TypePtr& type, std::size_t offset, std::size_t depth = 0) {
        if (depth > 128) throw std::runtime_error("聚合对象过深");
        if (object.cells.size() + object.bytes.size() > 16 * 1024 * 1024) throw std::runtime_error("聚合对象拷贝超出存储限制");
        if (detail::numeric(type)) {
            const auto size = detail::layout(type, symbols_).size;
            if (object.zero) for (std::size_t i = 0; i < size; ++i) {
                if (!object.unknown.count(offset + i)) object.bytes.emplace(offset + i, 0);
            }
            else for (std::size_t i = 0; i < size; ++i) if (!object.bytes.count(offset + i)) object.unknown.insert(offset + i);
        } else if (type->kind == TypeKind::Pointer) {
            if (!object.cells.count(offset)) object.cells[offset] = object.zero ? convert_value(std::int64_t{0}, type) : Value{std::monostate{}};
        } else if (type->kind == TypeKind::Array) {
            const auto stride = detail::layout(type->base, symbols_).size;
            for (std::size_t i = 0; i < *type->array_length; ++i) materialize_default(object, type->base, offset + i * stride, depth + 1);
        } else {
            const auto& members = symbols_.records.at(type->record_id).members;
            if (type->kind == TypeKind::Union) {
                const auto size = detail::layout(type, symbols_).size;
                for (std::size_t i = 0; i < size; ++i) {
                    const auto position = offset + i;
                    bool pointer_byte = false;
                    for (const auto& cell : object.cells)
                        if (position >= cell.first && position - cell.first < 8) { pointer_byte = true; break; }
                    if (pointer_byte) continue;
                    if (object.zero) { if (!object.unknown.count(position)) object.bytes.emplace(position, 0); }
                    else if (!object.bytes.count(position)) object.unknown.insert(position);
                }
            } else for (const auto& member : members) materialize_default(object, member.type, offset + *member.offset, depth + 1);
        }
    }

    void write(const std::string& name, const Value& value, Frame& frame) {
        auto converted = convert_value(value, destination_type(name, frame));
        if (auto* address = std::get_if<Address>(&converted); address && address->literal) {
            auto& literal = strings_[*address->literal]; if (!literal) literal = address->literal; address->literal = literal;
        }
        if (name.size() > 2 && name[1] == 's' && globals_.count(static_cast<SymbolId>(operand_id(name, 's'))))
            global_values_[name] = std::move(converted);
        else frame.values[name] = std::move(converted);
    }

    void write_address(const Address& address, const Value& value, const TypePtr& expected) {
        if (!address.type || !detail::same_unqualified(address.type, expected) || address.type->is_const) throw std::runtime_error("scanf 地址类型不匹配");
        store_address(address, value);
    }

    // 指针差值和大小比较只能在同一个数组范围内进行。
    void same_array(const Address& left, const Address& right) const {
        if (left.literal || right.literal) {
            if (left.literal != right.literal || left.first != right.first || left.last != right.last) throw std::runtime_error("指针不属于同一个字符串");
            return;
        }
        if ((left.symbol == invalid_id && left.temporary.empty()) || (right.symbol == invalid_id && right.temporary.empty()) || left.frame != right.frame ||
            left.symbol != right.symbol || left.first != right.first || left.last != right.last || left.temporary != right.temporary)
            throw std::runtime_error("指针差值或大小比较必须指向同一个数组");
    }

    Value builtin(const SymbolEntry& entry, const std::vector<Value>& arguments) {
        if (arguments.empty()) throw std::runtime_error("输入输出缺少格式字符串");
        const bool scanning = entry.builtin == BuiltinKind::Scanf;
        const auto parts = detail::format_parts(string_value(arguments[0]), scanning);
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
                TypeArena conversion_types;
                TypeArenaScope conversion_scope(conversion_types);
                Value input_value;
                const auto expected = detail::format_type(part, true);
                if (part.conversion == 's') {
                    auto address = std::get<Address>(value);
                    if (address.literal || !address.type || address.type->kind != TypeKind::Char || address.type->is_const)
                        throw std::runtime_error("scanf %s 需要可写字符数组");
                    const auto capacity = address.last - address.offset;
                    if (capacity < 1) throw std::runtime_error("scanf 字符数组没有容量");
                    input_ >> std::ws; std::string text;
                    const auto limit = part.width ? std::min(part.width, capacity - 1) : capacity - 1;
                    while (input_.peek() != std::char_traits<char>::eof() && !std::isspace(static_cast<unsigned char>(input_.peek())) && text.size() < limit)
                        text.push_back(static_cast<char>(input_.get()));
                    if (text.empty()) throw std::runtime_error("scanf 无法读取字符串");
                    if (!part.width && input_.peek() != std::char_traits<char>::eof() && !std::isspace(static_cast<unsigned char>(input_.peek())))
                        throw std::runtime_error("scanf 输入字符串超过数组容量");
                    for (const unsigned char byte : text) { store_address(address, std::int64_t{byte > 127 ? byte - 256 : byte}); ++address.offset; }
                    store_address(address, std::int64_t{0});
                    continue;
                } else if (part.conversion == 'd') {
                    std::int64_t integer = 0;
                    if (!(input_ >> integer)) throw std::runtime_error("scanf 无法读取整数");
                    input_value = convert_value(integer, expected);
                } else if (part.conversion == 'u' || part.conversion == 'x' || part.conversion == 'o') {
                    std::uint64_t integer = 0;
                    input_ >> (part.conversion == 'x' ? std::hex : part.conversion == 'o' ? std::oct : std::dec) >> integer;
                    input_ >> std::dec;
                    if (!input_ || integer >= (std::uint64_t{1} << detail::integer_bits(expected))) throw std::runtime_error("scanf 无符号输入超出范围");
                    input_value = static_cast<std::int64_t>(integer);
                } else if (part.conversion == 'f') {
                    double floating = 0;
                    if (!(input_ >> floating)) throw std::runtime_error("scanf 无法读取浮点数");
                    input_value = convert_value(floating, expected);
                } else {
                    char character = 0;
                    if (!input_.get(character)) throw std::runtime_error("scanf 无法读取字符");
                    const auto byte = static_cast<unsigned char>(character);
                    input_value = std::int64_t{byte > 127 ? byte - 256 : byte};
                }
                write_address(std::get<Address>(value), input_value, expected);
            } else {
                std::ostringstream formatted;
                if (part.conversion == 's') {
                    formatted << string_value(value);
                } else if (part.conversion == 'f') {
                    if (!std::holds_alternative<double>(value)) throw std::runtime_error("%f 需要 float");
                    formatted << std::fixed << std::setprecision(6) << std::get<double>(value);
                } else {
                    if (!std::holds_alternative<std::int64_t>(value)) throw std::runtime_error("%d/%c 需要整数");
                    const auto integer = std::get<std::int64_t>(value);
                    if (part.conversion == 'c') formatted << static_cast<char>(static_cast<unsigned char>(integer));
                    else if (part.conversion == 'u' || part.conversion == 'x' || part.conversion == 'o')
                        formatted << (part.conversion == 'x' ? std::hex : part.conversion == 'o' ? std::oct : std::dec) << static_cast<std::uint64_t>(integer);
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

    // 沿字符指针读取到终止零，越界或未初始化时立即报错。
    std::string string_value(const Value& value) {
        if (const auto* text = std::get_if<std::string>(&value)) return text->substr(0, text->find('\0'));
        if (!std::holds_alternative<Address>(value)) throw std::runtime_error("需要字符指针");
        auto address = std::get<Address>(value); std::string text;
        if (!address.type || address.type->kind != TypeKind::Char) throw std::runtime_error("字符串指针类型错误");
        while (address.offset < address.last) {
            const auto byte = std::get<std::int64_t>(read_address(address));
            if (byte == 0) return text;
            text.push_back(static_cast<char>(static_cast<unsigned char>(byte))); ++address.offset;
        }
        throw std::runtime_error("字符串缺少终止零或读取越界");
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
            global_values_[detail::symbol_name(id)] = initial_value(symbol(id).type, true);
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
            if (q.op == "local" || q.op == "zero") {
                const auto& entry = symbol(operand_id(q.result, 's'));
                auto value = initial_value(entry.type, q.op == "zero");
                if (globals_.count(entry.id)) global_values_[q.result] = std::move(value);
                else frame.values[q.result] = std::move(value);
                continue;
            }
            if (q.op == "jmp" || q.op == "jz" || q.op == "jnz") {
                const bool jump = q.op == "jmp" || (q.op == "jz" ? !truth(read(q.arg1, frame)) : truth(read(q.arg1, frame)));
                if (jump) frame.pc = frame.code->labels.at(q.result);
            } else if (q.op == "=" || q.op == "cvt" || q.op == "cvt_i2f") write(q.result, read(q.arg1, frame), frame);
            else if (q.op == "faddr") {
                const auto& entry = function(q.arg1);
                write(q.result, Address{0, entry.id, 0, entry.type, 0, 0, {}, {}}, frame);
            } else if (q.op == "tempaddr") {
                const auto type = destination_type(q.arg1, frame);
                write(q.result, Address{frame.id, invalid_id, 0, type, 0, detail::layout(type, symbols_).size, {}, q.arg1}, frame);
            } else if (q.op == "addr") {
                const auto& entry = symbol(operand_id(q.arg1, 's'));
                write(q.result, Address{globals_.count(entry.id) ? 0 : frame.id, entry.id, 0, entry.type, 0, detail::layout(entry.type, symbols_).size, {}, {}}, frame);
            } else if (q.op == "decay" || q.op == "ptradd" || q.op == "ptrsub") {
                const auto value = read(q.arg1, frame);
                if (!std::holds_alternative<Address>(value)) throw std::runtime_error("指针偏移需要对象地址");
                auto selected = std::get<Address>(value);
                if (selected.symbol == invalid_id && !selected.literal && selected.temporary.empty()) throw std::runtime_error("空指针不能偏移");
                if (q.op == "decay") {
                    root_value(selected);
                    selected.first = selected.offset;
                    selected.last = selected.offset + detail::layout(selected.type, symbols_).size;
                } else {
                    const auto index_value = read(q.arg2, frame);
                    if (!std::holds_alternative<std::int64_t>(index_value)) throw std::runtime_error("指针偏移需要整型下标");
                    const auto stride = detail::layout(selected.type, symbols_).size;
                    const auto delta = std::get<std::int64_t>(index_value) * static_cast<std::int64_t>(stride) * (q.op == "ptrsub" ? -1 : 1);
                    const auto offset = static_cast<std::int64_t>(selected.offset) + delta;
                    if (offset < static_cast<std::int64_t>(selected.first) || offset > static_cast<std::int64_t>(selected.last))
                        throw std::runtime_error("指针偏移越界");
                    selected.offset = static_cast<std::size_t>(offset);
                }
                selected.type = destination_type(q.result, frame)->base;
                write(q.result, selected, frame);
            } else if (q.op == "ptrdiff") {
                const auto a = read(q.arg1, frame), b = read(q.arg2, frame);
                if (!std::holds_alternative<Address>(a) || !std::holds_alternative<Address>(b)) throw std::runtime_error("指针差值需要对象地址");
                const auto& left = std::get<Address>(a); const auto& right = std::get<Address>(b); same_array(left, right);
                const auto stride = static_cast<std::int64_t>(detail::layout(left.type, symbols_).size);
                write(q.result, (static_cast<std::int64_t>(left.offset) - static_cast<std::int64_t>(right.offset)) / stride, frame);
            } else if (q.op == "indexaddr" || q.op == "memberaddr") {
                const auto base_value = read(q.arg1, frame);
                if (!std::holds_alternative<Address>(base_value)) throw std::runtime_error("寻址操作数不是对象地址");
                auto selected = std::get<Address>(base_value);
                root_value(selected);
                if (q.op == "indexaddr") {
                    const auto index_value = read(q.arg2, frame);
                    if (!std::holds_alternative<std::int64_t>(index_value) || !selected.type || selected.type->kind != TypeKind::Array)
                        throw std::runtime_error("数组寻址的下标或地址类型无效");
                    const auto index = std::get<std::int64_t>(index_value);
                    if (index < 0 || static_cast<std::size_t>(index) >= *selected.type->array_length)
                        throw std::runtime_error("数组下标越界");
                    selected.first = selected.offset;
                    selected.last = selected.offset + detail::layout(selected.type, symbols_).size;
                    selected.offset += static_cast<std::size_t>(index) * detail::layout(selected.type->base, symbols_).size;
                } else {
                    selected.offset += decimal(q.arg2); selected.first = selected.offset;
                    selected.last = selected.offset + detail::layout(destination_type(q.result, frame)->base, symbols_).size;
                }
                selected.type = destination_type(q.result, frame)->base;
                root_value(selected);
                write(q.result, selected, frame);
            } else if (q.op == "load") {
                const auto pointer = read(q.arg1, frame);
                if (!std::holds_alternative<Address>(pointer)) throw std::runtime_error("load 需要对象地址");
                write(q.result, read_address(std::get<Address>(pointer)), frame);
            } else if (q.op == "store") {
                const auto pointer = read(q.result, frame);
                if (!std::holds_alternative<Address>(pointer)) throw std::runtime_error("store 需要对象地址");
                store_address(std::get<Address>(pointer), read(q.arg1, frame));
            } else if (q.op == "arg") frame.arguments.push_back(read(q.arg1, frame));
            else if (q.op == "call" || q.op == "callind") {
                SymbolId called;
                if (q.op == "call") called = function(q.arg1).id;
                else {
                    const auto pointer = read(q.arg1, frame);
                    if (!std::holds_alternative<Address>(pointer) || std::get<Address>(pointer).symbol == invalid_id) throw std::runtime_error("不能调用空函数指针");
                    const auto& address = std::get<Address>(pointer);
                    const auto& candidate = function(detail::symbol_name(address.symbol));
                    if (address.frame != 0 || address.offset != 0 || !same_type(candidate.type, destination_type(q.arg1, frame)->base))
                        throw std::runtime_error("函数指针身份或签名无效");
                    called = candidate.id;
                }
                const auto& callee = symbol(called);
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
            } else if (q.op == "neg" || q.op == "not" || q.op == "bnot") {
                const auto value = read(q.arg1, frame);
                write(q.result, q.op == "not" ? Value{std::int64_t{!truth(value)}} :
                    q.op == "bnot" ? Value{~std::get<std::int64_t>(value)} :
                    std::holds_alternative<std::int64_t>(value) ? Value{-std::get<std::int64_t>(value)} : Value{-number(value)}, frame);
            } else {
                const auto left_value = read(q.arg1, frame);
                const auto right_value = read(q.arg2, frame);
                if (std::holds_alternative<Address>(left_value) || std::holds_alternative<Address>(right_value)) {
                    if (!std::holds_alternative<Address>(left_value) || !std::holds_alternative<Address>(right_value) ||
                        (q.op != "==" && q.op != "!=" && q.op != "<" && q.op != "<=" && q.op != ">" && q.op != ">=")) throw std::runtime_error("指针运算类型无效");
                    const auto& a = std::get<Address>(left_value); const auto& b = std::get<Address>(right_value);
                    const bool equal = a.frame == b.frame && a.symbol == b.symbol && a.offset == b.offset && a.literal == b.literal && a.temporary == b.temporary;
                    bool compared;
                    if (q.op == "==" || q.op == "!=") compared = q.op == "==" ? equal : !equal;
                    else {
                        same_array(a, b);
                        compared = q.op == "<" ? a.offset < b.offset : q.op == "<=" ? a.offset <= b.offset :
                            q.op == ">" ? a.offset > b.offset : a.offset >= b.offset;
                    }
                    write(q.result, std::int64_t{compared}, frame);
                    continue;
                }
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
                    const auto target = destination_type(q.result, frame);
                    if (target->is_unsigned && q.op == "*") {
                        const auto product = static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b);
                        write(q.result, static_cast<std::int64_t>(product & ((std::uint64_t{1} << detail::integer_bits(target)) - 1)), frame);
                        continue;
                    }
                    if ((q.op == "/" || q.op == "%") && b == 0) throw std::runtime_error("整数除零");
                    if ((q.op == "/" || q.op == "%") && a == std::numeric_limits<std::int32_t>::min() && b == -1)
                        throw std::runtime_error("整数除法溢出");
                    if (q.op == "+") value = a + b;
                    else if (q.op == "-") value = a - b;
                    else if (q.op == "*") value = a * b;
                    else if (q.op == "/") value = a / b;
                    else if (q.op == "%") value = a % b;
                    else if (q.op == "&") value = a & b;
                    else if (q.op == "|") value = a | b;
                    else if (q.op == "^") value = a ^ b;
                    else if (q.op == "<<" || q.op == ">>") {
                        if (b < 0 || b >= detail::integer_bits(target) || (q.op == "<<" && a < 0)) throw std::runtime_error("移位数量或左移操作数无效");
                        const auto divisor = std::int64_t{1} << b;
                        if (target->is_unsigned && q.op == "<<") value = static_cast<std::int64_t>((static_cast<std::uint64_t>(a) << b) & 0xffffffffu);
                        else value = q.op == "<<" ? a * divisor : (a >= 0 ? a / divisor : -((-a + divisor - 1) / divisor));
                    } else throw std::runtime_error("整型运算符无效");
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
    TypeArena types;
    TypeArenaScope type_scope(types);
    return Machine(program, symbols, input, output, options).run();
}

}
