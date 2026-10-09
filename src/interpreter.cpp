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
    SymbolId symbol = invalid_id; // 所指对象编号，invalid_id 表示空指针。
    std::size_t offset = 0; // 子对象相对根对象的字节偏移。
    TypePtr type; // 当前地址所指向的子对象类型。
    std::size_t first = 0; // 可移动范围的起点，包含此偏移。
    std::size_t last = 0; // 可移动范围的终点，仅允许形成尾后指针。
};
struct Aggregate;
using Value = std::variant<std::monostate, std::int64_t, double, std::string, Address, std::shared_ptr<Aggregate>>;
struct Aggregate {
    std::unordered_map<std::size_t, Value> cells; // 按字节偏移保存已写入的数值叶子。
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
    if (const auto* address = std::get_if<Address>(&value)) return address->symbol != invalid_id;
    if (std::holds_alternative<std::string>(value)) return true;
    return number(value) != 0;
}

bool assign_type(const TypePtr& target, const TypePtr& source) {
    return same_type(target, source) || can_assign(target, source) || detail::pointer_assign(target, source);
}

bool compatible_subobject(const TypePtr& target, const TypePtr& member, bool parent_const, bool parent_volatile) {
    if (!target || !member) return false;
    auto qualified = std::make_shared<TypeInfo>(*member);
    qualified->is_const = qualified->is_const || parent_const;
    qualified->is_volatile = qualified->is_volatile || parent_volatile;
    return same_type(target, qualified);
}

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
        if (const auto* address = std::get_if<Address>(&value)) {
            auto converted = *address; converted.type = target->base; return converted;
        }
        if (const auto* integer = std::get_if<std::int64_t>(&value); integer && *integer == 0)
            return Address{0, invalid_id, 0, target->base, 0, 0};
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
                    target->kind != TypeKind::Pointer || !detail::numeric(index) || index->kind == TypeKind::Float ||
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
                    !detail::numeric(index) || index->kind == TypeKind::Float) throw std::runtime_error("指针偏移类型无效");
                detail::layout(base->base, symbols_);
            } else if (q.op == "ptrdiff") {
                const auto left = input_type(q.arg1), right = input_type(q.arg2);
                if (!(detail::pointer_assign(left, right) || detail::pointer_assign(right, left)) || destination_type()->kind != TypeKind::Int)
                    throw std::runtime_error("指针差值类型无效");
                detail::layout(left->base, symbols_);
            } else if (q.op == "memberaddr") {
                const auto base = input_type(q.arg1), target = destination_type();
                if (base->kind != TypeKind::Pointer || !base->base || base->base->kind != TypeKind::Struct ||
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
            } else if (q.op == "call") {
                const auto& callee = function(q.arg1);
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
                    (target->kind == TypeKind::Pointer && detail::numeric(source) && source->kind != TypeKind::Float &&
                     q.arg1.size() > 2 && q.arg1[1] == 'c' &&
                     std::get<std::int64_t>(program_.constants.entries.at(operand_id(q.arg1, 'c')).value) == 0) ||
                    (source->kind == TypeKind::Array && source->base && source->base->kind == TypeKind::Char &&
                     target->kind == TypeKind::Pointer && target->base->kind == TypeKind::Char)))
                    throw std::runtime_error("不支持该转换指令");
            } else if (q.op == "neg" || q.op == "not" || q.op == "bnot") {
                if (q.op == "not") require_scalar(q.arg1); else require_numeric(q.arg1);
                if (q.arg2 != "-" || !detail::numeric(destination_type()) ||
                    (q.op == "not" && destination_type()->kind != TypeKind::Int) ||
                    (q.op == "bnot" && (input_type(q.arg1)->kind == TypeKind::Float || destination_type()->kind != TypeKind::Int))) throw std::runtime_error("一元指令类型无效");
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
                    (integral && (input_type(q.arg1)->kind == TypeKind::Float || input_type(q.arg2)->kind == TypeKind::Float || destination_type()->kind == TypeKind::Float)))
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
            if ((entry.kind != SymbolKind::Variable && entry.kind != SymbolKind::Array) ||
                (entry.scope != 0 && entry.storage != StorageClass::Static) || !globals_.insert(id).second)
                throw std::runtime_error("全局变量清单无效");
            detail::layout(entry.type, symbols_);
        }
        for (const auto& function_ir : program_.functions) {
            const auto& entry = symbol(function_ir.symbol_id);
            if (entry.kind != SymbolKind::Function || !entry.is_defined || entry.builtin != BuiltinKind::None ||
                !entry.type || entry.type->kind != TypeKind::Function || !entry.type->base || entry.type->variadic ||
                !(detail::numeric(entry.type->base) || entry.type->base->kind == TypeKind::Void || entry.type->base->kind == TypeKind::Pointer))
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
        if (address.symbol == invalid_id) throw std::runtime_error("不能解引用空指针");
        const auto& entry = symbol(address.symbol);
        if (!address.type) throw std::runtime_error("地址缺少所指类型");
        const auto total = detail::layout(entry.type, symbols_).size;
        const auto selected = detail::layout(address.type, symbols_).size;
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
        auto& root = root_value(address);
        const auto& root_type = symbol(address.symbol).type;
        if (!detail::aggregate(root_type)) {
            if (std::holds_alternative<std::monostate>(root)) throw std::runtime_error("读取尚未初始化的变量");
            return root;
        }
        const auto object = std::get<std::shared_ptr<Aggregate>>(root);
        if (detail::aggregate(address.type)) {
            auto result = std::make_shared<Aggregate>(); result->zero = object->zero;
            const auto size = detail::layout(address.type, symbols_).size;
            for (const auto& cell : object->cells)
                if (cell.first >= address.offset && cell.first - address.offset < size)
                    result->cells.emplace(cell.first - address.offset, cell.second);
            return result;
        }
        const auto cell = object->cells.find(address.offset);
        if (cell != object->cells.end()) {
            if (std::holds_alternative<std::monostate>(cell->second)) throw std::runtime_error("读取尚未初始化的聚合子对象");
            return cell->second;
        }
        if (!object->zero) throw std::runtime_error("读取尚未初始化的数组元素或结构体成员");
        return convert_value(std::int64_t{0}, address.type);
    }

    void store_address(const Address& address, const Value& value) {
        auto converted = convert_value(value, address.type);
        auto& root = root_value(address);
        if (!detail::aggregate(symbol(address.symbol).type)) { root = std::move(converted); return; }
        auto object = std::get<std::shared_ptr<Aggregate>>(root);
        if (!detail::aggregate(address.type)) { object->cells[address.offset] = std::move(converted); return; }
        const auto size = detail::layout(address.type, symbols_).size;
        auto source = std::get<std::shared_ptr<Aggregate>>(converted);
        for (auto it = object->cells.begin(); it != object->cells.end();) {
            if (it->first >= address.offset && it->first - address.offset < size) it = object->cells.erase(it);
            else ++it;
        }
        if (address.offset == 0 && same_type(address.type, symbol(address.symbol).type)) object->zero = source->zero;
        else if (source->zero != object->zero) materialize_default(*source, address.type, 0);
        for (const auto& cell : source->cells) object->cells[address.offset + cell.first] = cell.second;
    }

    // 子聚合的默认状态与父对象不同时，显式保留每个数值叶子的状态。
    void materialize_default(Aggregate& object, const TypePtr& type, std::size_t offset, std::size_t depth = 0) {
        if (depth > 128) throw std::runtime_error("聚合对象过深");
        if (object.cells.size() > 1'000'000) throw std::runtime_error("聚合对象拷贝超出叶子数量限制");
        if (detail::numeric(type) || type->kind == TypeKind::Pointer) {
            if (!object.cells.count(offset)) object.cells[offset] = object.zero ? convert_value(std::int64_t{0}, type) : Value{std::monostate{}};
        } else if (type->kind == TypeKind::Array) {
            const auto stride = detail::layout(type->base, symbols_).size;
            for (std::size_t i = 0; i < *type->array_length; ++i) materialize_default(object, type->base, offset + i * stride, depth + 1);
        } else for (const auto& member : symbols_.records.at(type->record_id).members)
            materialize_default(object, member.type, offset + *member.offset, depth + 1);
    }

    void write(const std::string& name, const Value& value, Frame& frame) {
        auto converted = convert_value(value, destination_type(name, frame));
        if (name.size() > 2 && name[1] == 's' && globals_.count(static_cast<SymbolId>(operand_id(name, 's'))))
            global_values_[name] = std::move(converted);
        else frame.values[name] = std::move(converted);
    }

    void write_address(const Address& address, const Value& value, TypeKind expected) {
        if (!address.type || address.type->kind != expected || address.type->is_const) throw std::runtime_error("scanf 地址类型不匹配");
        store_address(address, value);
    }

    // 指针差值和大小比较只能在同一个数组范围内进行。
    void same_array(const Address& left, const Address& right) const {
        if (left.symbol == invalid_id || right.symbol == invalid_id || left.frame != right.frame ||
            left.symbol != right.symbol || left.first != right.first || left.last != right.last)
            throw std::runtime_error("指针差值或大小比较必须指向同一个数组");
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
            else if (q.op == "addr") {
                const auto& entry = symbol(operand_id(q.arg1, 's'));
                write(q.result, Address{globals_.count(entry.id) ? 0 : frame.id, entry.id, 0, entry.type, 0, detail::layout(entry.type, symbols_).size}, frame);
            } else if (q.op == "decay" || q.op == "ptradd" || q.op == "ptrsub") {
                const auto value = read(q.arg1, frame);
                if (!std::holds_alternative<Address>(value)) throw std::runtime_error("指针偏移需要对象地址");
                auto selected = std::get<Address>(value);
                if (selected.symbol == invalid_id) throw std::runtime_error("空指针不能偏移");
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
            } else if (q.op == "neg" || q.op == "not" || q.op == "bnot") {
                const auto value = read(q.arg1, frame);
                write(q.result, q.op == "not" ? Value{std::int64_t{!truth(value)}} :
                    q.op == "bnot" ? Value{~std::get<std::int64_t>(value)} : Value{-number(value)}, frame);
            } else {
                const auto left_value = read(q.arg1, frame);
                const auto right_value = read(q.arg2, frame);
                if (std::holds_alternative<Address>(left_value) || std::holds_alternative<Address>(right_value)) {
                    if (!std::holds_alternative<Address>(left_value) || !std::holds_alternative<Address>(right_value) ||
                        (q.op != "==" && q.op != "!=" && q.op != "<" && q.op != "<=" && q.op != ">" && q.op != ">=")) throw std::runtime_error("指针运算类型无效");
                    const auto& a = std::get<Address>(left_value); const auto& b = std::get<Address>(right_value);
                    const bool equal = a.frame == b.frame && a.symbol == b.symbol && a.offset == b.offset;
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
                    if ((q.op == "/" || q.op == "%") && b == 0) throw std::runtime_error("整数除零");
                    if ((q.op == "/" || q.op == "%") && a == std::numeric_limits<std::int32_t>::min() && b == -1)
                        throw std::runtime_error("整数除法溢出");
                    if (q.op == "+") value = detail::checked_integer(a + b);
                    else if (q.op == "-") value = detail::checked_integer(a - b);
                    else if (q.op == "*") value = detail::checked_integer(a * b);
                    else if (q.op == "/") value = detail::checked_integer(a / b);
                    else if (q.op == "%") value = detail::checked_integer(a % b);
                    else if (q.op == "&") value = a & b;
                    else if (q.op == "|") value = a | b;
                    else if (q.op == "^") value = a ^ b;
                    else if (q.op == "<<" || q.op == ">>") {
                        if (b < 0 || b >= 32 || (q.op == "<<" && a < 0)) throw std::runtime_error("移位数量或左移操作数无效");
                        const auto divisor = std::int64_t{1} << b;
                        value = detail::checked_integer(q.op == "<<" ? a * divisor : (a >= 0 ? a / divisor : -((-a + divisor - 1) / divisor)));
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
    return Machine(program, symbols, input, output, options).run();
}

}
