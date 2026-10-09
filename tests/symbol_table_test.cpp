#include "minic/symbol_table.hpp"
#include "minic/semantic.hpp"

#include <iostream>
#include <stdexcept>
#include <utility>

using namespace minic;

static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

static TypePtr basic(TypeKind kind) {
    auto type = std::make_shared<TypeInfo>();
    type->kind = kind;
    return type;
}

static TypePtr indirect(TypeKind kind, TypePtr base,
                        std::optional<std::size_t> length = std::nullopt) {
    auto type = std::make_shared<TypeInfo>();
    type->kind = kind;
    type->base = std::move(base);
    type->array_length = length;
    return type;
}

static TypePtr function(TypePtr result, std::vector<TypePtr> params = {}) {
    auto type = std::make_shared<TypeInfo>();
    type->kind = TypeKind::Function;
    type->base = std::move(result);
    type->params = std::move(params);
    return type;
}

static SourceRange at(std::size_t offset) {
    return {"test.c", {3, 5, offset}, {3, 6, offset + 1}};
}

static SymbolEntry declaration(const std::string& name, std::size_t offset = 10,
                               SymbolKind kind = SymbolKind::Variable,
                               TypePtr type = basic(TypeKind::Int)) {
    SymbolEntry entry;
    entry.name = name;
    entry.kind = kind;
    entry.type = std::move(type);
    entry.range = at(offset);
    return entry;
}

static void test_diagnostics() {
    bool threw = false;
    try { DiagnosticEngine invalid(Phase::Lexer, 0); }
    catch (const std::invalid_argument&) { threw = true; }
    require(threw, "错误上限 0 必须拒绝");
    DiagnosticEngine engine(Phase::Semantic, 2);
    engine.report(Level::Warning, at(1), "警告");
    engine.report(Level::Note, at(2), "说明");
    require(engine.error_count() == 0 && !engine.should_stop(), "警告和说明不能计入错误");
    engine.report(Level::Error, at(3), "变量未声明", "SEM_UNDECLARED", {at(1)});
    const auto& first = engine.diagnostics()[2];
    require(first.phase == Phase::Semantic && first.line == 3 && first.col == 5 &&
            first.range.begin.offset == 3 && first.related[0].begin.offset == 1,
            "诊断阶段、位置和关联声明必须保存");
    engine.report(Level::Error, at(4), "真实原因", "SECOND");
    engine.report(Level::Warning, at(5), "忽略");
    require(engine.should_stop() && engine.error_count() == 2 && engine.diagnostics().size() == 4,
            "达到上限后应忽略后续所有报告");
    require(engine.diagnostics().back().code == "SECOND" &&
            engine.diagnostics().back().message == "真实原因；错误过多，停止本阶段",
            "达到上限应保留实际原因和错误代码");
    auto entries = engine.take_diagnostics();
    require(entries.size() == 4 && engine.diagnostics().empty() &&
            engine.error_count() == 0 && !engine.should_stop(), "取走诊断应重置状态");
    engine.report(Level::Fatal, at(6), "致命错误");
    engine.report(Level::Error, at(7), "忽略");
    require(engine.should_stop() && engine.error_count() == 1 && engine.diagnostics().size() == 1,
            "Fatal 必须立即停止");
    engine.take_diagnostics();
    engine.report(Level::Error, at(8), "重新收集");
    require(!engine.should_stop(), "Fatal 标记必须随取走诊断而重置");
    DiagnosticEngine single(Phase::Parser, 1);
    single.report(Level::Error, at(1), "缺少分号", "SEMI");
    require(single.diagnostics()[0].message.find("缺少分号") == 0, "上限 1 也要保留实际错误");
    DiagnosticEngine defaults(Phase::Semantic);
    for (int i = 0; i < 25; ++i) defaults.report(Level::Error, at(1), "错误");
    require(defaults.error_count() == 20 && defaults.diagnostics().size() == 20,
            "默认上限应为 20");
}

static void test_types() {
    const auto character = basic(TypeKind::Char);
    const auto integer = basic(TypeKind::Int);
    const auto floating = basic(TypeKind::Float);
    require(same_type(integer, basic(TypeKind::Int)), "同型不能依赖共享指针地址");
    require(!same_type(nullptr, nullptr) && !same_type(basic(TypeKind::Error), basic(TypeKind::Error)) &&
            !same_type(basic(TypeKind::Unknown), basic(TypeKind::Unknown)) &&
            !same_type(basic(TypeKind::Named), basic(TypeKind::Named)), "无效或未解析类型不能相等");
    auto qualified = std::make_shared<TypeInfo>(*integer);
    qualified->is_const = true;
    require(!same_type(integer, qualified) && can_assign(qualified, integer),
            "结构相等比较限定符，赋值类型规则不检查左值 const");
    qualified->is_const = false;
    qualified->is_volatile = true;
    require(!same_type(integer, qualified), "volatile 参与类型比较");
    qualified->is_volatile = false;
    qualified->is_unsigned = true;
    require(!same_type(integer, qualified) && can_assign(integer, qualified) &&
            arithmetic_result(integer, qualified)->is_unsigned,
            "无符号参与公共类型及数值转换");
    const TypePtr types[] = {character, integer, floating};
    const bool assignment[3][3] = {{true, false, false}, {true, true, false}, {true, true, true}};
    for (std::size_t target = 0; target < 3; ++target) {
        for (std::size_t source = 0; source < 3; ++source) {
            require(can_assign(types[target], types[source]) == assignment[target][source],
                    "M1 赋值转换矩阵错误");
            require(arithmetic_result(types[target], types[source])->kind ==
                    (target == 2 || source == 2 ? TypeKind::Float : TypeKind::Int),
                    "M1 算术提升矩阵错误");
        }
    }
    require(arithmetic_result(nullptr, integer)->kind == TypeKind::Error &&
            !can_assign(integer, basic(TypeKind::Void)), "非法和非数值类型应拒绝");
    require(same_type(indirect(TypeKind::Pointer, integer), indirect(TypeKind::Pointer, basic(TypeKind::Int))) &&
            !same_type(indirect(TypeKind::Pointer, integer), indirect(TypeKind::Pointer, character)),
            "指针应比较所指类型");
    require(!same_type(indirect(TypeKind::Array, integer, 2), indirect(TypeKind::Array, integer, 3)) &&
            same_type(indirect(TypeKind::Array, integer, 2), indirect(TypeKind::Array, integer, 2)),
            "数组应比较元素和长度");
    auto record_type = std::make_shared<TypeInfo>();
    record_type->kind = TypeKind::Struct;
    require(!same_type(record_type, record_type), "记录类型必须有身份");
    record_type->record_id = 0;
    auto other = std::make_shared<TypeInfo>(*record_type);
    require(same_type(record_type, other), "同记录编号应相同");
    other->record_id = 1;
    require(!same_type(record_type, other), "同名记录不能代替记录编号");
    const auto signature = function(integer, {character});
    require(same_type(signature, function(basic(TypeKind::Int), {basic(TypeKind::Char)})) &&
            !same_type(signature, function(integer, {integer})) &&
            !same_type(signature, function(floating, {character})), "函数应比较返回类型和参数");
    auto different = std::make_shared<TypeInfo>(*signature);
    different->variadic = true;
    require(!same_type(signature, different), "可变参数标记参与签名比较");
    different->variadic = false;
    different->has_prototype = false;
    require(!same_type(signature, different), "原型标记参与签名比较");
    auto constant = std::make_shared<TypeInfo>(*integer); constant->is_const = true;
    require(same_type(function(integer, {integer}), function(integer, {constant})), "形参顶层 const 不影响签名");
    require(!same_type(function(integer, {indirect(TypeKind::Pointer, integer)}),
                       function(integer, {indirect(TypeKind::Pointer, constant)})), "所指对象的 const 仍影响签名");
    require(same_type(function(integer, {indirect(TypeKind::Array, integer, 3)}),
                      function(integer, {indirect(TypeKind::Pointer, integer)})), "数组形参应按指针比较");
}

static void test_scopes_and_functions() {
    DiagnosticEngine diagnostics(Phase::Semantic, 100);
    SymbolTable table(diagnostics);
    require(table.current_scope() == 0 && table.data().scopes[0].parent == invalid_id &&
            table.data().symbols.empty(), "构造应只创建空的全局作用域");
    bool threw = false;
    try { table.enter_scope(ScopeKind::Global, at(1)); }
    catch (const std::invalid_argument&) { threw = true; }
    require(threw && table.data().scopes.size() == 1, "重复全局作用域必须拒绝");
    auto original = declaration("x");
    original.id = 99;
    original.scope = 99;
    const auto global = table.insert(original).value();
    require(global == 0 && table.symbol(global)->scope == 0 && table.symbol(global)->line == 3 &&
            table.symbol(global)->col == 5, "符号身份和位置应由表同步");
    require(!table.insert(declaration("x")) && table.data().symbols.size() == 1 &&
            table.lookup("x") == global && diagnostics.diagnostics().back().related.size() == 1,
            "同层冲突不能破坏旧记录，且要提供旧位置");
    const auto scope = table.enter_scope(ScopeKind::Function, at(20));
    require(!table.lookup_current("x") && table.lookup("x") == global, "当前层查找不能包含外层");
    const auto parameter = table.insert(declaration("x", 25, SymbolKind::Parameter)).value();
    require(!table.insert(declaration("x", 30)), "形参与最外层函数体变量应同层冲突");
    table.enter_scope(ScopeKind::Block, at(40));
    const auto local = table.insert(declaration("x", 45)).value();
    require(table.lookup("x") == local && table.set_symbol_offset(local, 8) &&
            table.symbol(local)->offset == 8, "内层应遮蔽外层并允许设置对象偏移");
    require(table.exit_scope() && table.lookup("x") == parameter, "退出块应恢复形参可见性");
    require(table.exit_scope() && table.lookup("x") == global && table.symbol(local) != nullptr &&
            table.data().scopes[scope].parent == 0, "退出函数应保留条目和父作用域");
    require(!table.lookup("missing") && !table.symbol(invalid_id) &&
            !table.set_symbol_offset(invalid_id, 0), "非法编号不能越界");
    const auto first = table.insert(declaration("sum", 50, SymbolKind::Function,
                                                function(basic(TypeKind::Int), {basic(TypeKind::Int)}))).value();
    auto prototype = declaration("sum", 60, SymbolKind::Function,
                                  function(basic(TypeKind::Int), {basic(TypeKind::Int)}));
    require(table.insert(prototype) == first, "相同函数原型应复用编号");
    prototype.is_defined = true;
    require(table.insert(prototype) == first && table.symbol(first)->is_defined &&
            table.symbol(first)->range.begin.offset == 50, "定义应合并并保留首次位置");
    require(!table.insert(prototype) && !table.set_symbol_offset(first, 0),
            "重复函数定义和函数偏移应拒绝");
    prototype.is_defined = false;
    prototype.type = function(basic(TypeKind::Float), {basic(TypeKind::Int)});
    require(!table.insert(prototype) && table.symbol(first)->type->base->kind == TypeKind::Int,
            "冲突签名不能覆盖旧类型");
    require(!table.insert(declaration("")) && !table.insert(declaration("bad", 1, SymbolKind::Variable, nullptr)),
            "无名或无类型符号应拒绝");
    // 大量追加后，旧编号仍然对应同一个声明。
    for (int i = 0; i < 300; ++i) table.insert(declaration("temp" + std::to_string(i)));
    require(table.symbol(global)->name == "x" && table.symbol(local)->name == "x",
            "扩容不能改变编号");
    auto data = std::move(table).release();
    require(data.symbols[first].name == "sum" && data.active_scopes == std::vector<ScopeId>{0},
            "release 应移交完整表数据");
    SymbolTable second(diagnostics);
    require(!second.exit_scope() && diagnostics.should_stop(), "退出全局必须报告 Fatal");
}

static void test_records() {
    DiagnosticEngine diagnostics(Phase::Semantic, 100);
    SymbolTable table(diagnostics);
    const auto ordinary = table.insert(declaration("Node")).value();
    const auto node = table.declare_record("Node", RecordKind::Struct, at(10)).value();
    require(table.lookup("Node") == ordinary && table.lookup_tag("Node") == node,
            "普通名字与标签应使用独立名字表");
    require(table.declare_record("Node", RecordKind::Struct, at(20)) == node &&
            !table.declare_record("Node", RecordKind::Union, at(20)), "前向声明应复用，种类冲突应拒绝");
    require(!table.find_member(node, "value") && !table.record(invalid_id), "不完整记录不能查成员");
    auto node_type = std::make_shared<TypeInfo>();
    node_type->kind = TypeKind::Struct;
    node_type->record_id = node;
    StructEntry definition = *table.record(node);
    definition.size = 16;
    definition.alignment = 8;
    definition.members = {{"value", basic(TypeKind::Int), 0, at(30)},
                          {"next", indirect(TypeKind::Pointer, node_type), 8, at(40)}};
    auto bad = definition;
    bad.members[1].name = "value";
    require(!table.complete_record(node, bad) && !table.record(node)->is_complete &&
            table.record(node)->members.empty(), "重复成员失败不能留下半成品");
    bad = definition;
    bad.members[1].offset.reset();
    require(!table.complete_record(node, bad), "缺少成员偏移应拒绝");
    bad = definition;
    bad.id = invalid_id;
    require(!table.complete_record(node, bad), "错误记录身份应拒绝");
    bad = definition;
    bad.members[1].type = node_type;
    require(!table.complete_record(node, bad), "记录不能直接包含自身，只能用指针");
    bad = definition;
    bad.alignment = 3;
    require(!table.complete_record(node, bad), "非法对齐应拒绝");
    require(table.complete_record(node, definition) && table.record(node)->is_complete &&
            table.find_member(node, "next") == 1 && !table.find_member(node, "unknown"),
            "完成定义后应能按成员名查下标");
    require(!table.complete_record(node, definition) && table.record(node)->members.size() == 2,
            "重复记录定义不能覆盖旧成员");
    table.enter_scope(ScopeKind::Block, at(50));
    require(!table.lookup_tag_current("Node") && table.lookup_tag("Node") == node,
            "标签查找应从内向外");
    const auto inner = table.declare_record("Node", RecordKind::Union, at(60)).value();
    require(inner != node && table.lookup_tag("Node") == inner, "内层标签可遮蔽不同种类的外层标签");
    auto union_definition = *table.record(inner);
    union_definition.size = 4;
    union_definition.alignment = 4;
    union_definition.members = {{"number", basic(TypeKind::Int), 1, at(60)}};
    require(!table.complete_record(inner, union_definition), "联合体成员偏移应为 0");
    union_definition.members[0].offset = 0;
    require(table.complete_record(inner, union_definition), "有效联合体应可完成");
    table.exit_scope();
    require(table.lookup_tag("Node") == node, "退出作用域应恢复外层标签");
    const auto anonymous = table.declare_record("", RecordKind::Struct, at(70)).value();
    require(anonymous != table.declare_record("", RecordKind::Struct, at(71)).value() &&
            !table.lookup_tag(""), "匿名记录不能加入标签名映射");
    const auto enumeration = table.declare_record("Color", RecordKind::Enum, at(80)).value();
    auto enum_definition = *table.record(enumeration);
    enum_definition.size = 4;
    enum_definition.alignment = 4;
    enum_definition.enumerators = {{"Red", 0, invalid_id, at(81)}};
    require(!table.complete_record(enumeration, enum_definition), "枚举项必须先登记普通符号");
    enum_definition.enumerators[0].symbol_id = table.insert(declaration("Red", 81, SymbolKind::EnumConstant)).value();
    require(table.complete_record(enumeration, enum_definition) && table.lookup("Red") &&
            !table.find_member(enumeration, "Red"), "枚举项属于普通名字，不属于成员表");
    require(!table.complete_record(invalid_id, definition), "非法记录编号不能越界");
}

static void test_builtins_and_completion() {
    DiagnosticEngine diagnostics(Phase::Semantic, 100);
    SymbolTable table(diagnostics);
    require(register_builtins(table) && register_builtins(table) && table.data().symbols.size() == 2,
            "内建登记应幂等");
    const auto printf_id = table.lookup("printf").value();
    const auto* printf_entry = table.symbol(printf_id);
    require(printf_entry->builtin == BuiltinKind::Printf && printf_entry->type->variadic &&
            printf_entry->type->base->kind == TypeKind::Int &&
            printf_entry->type->params[0]->base->is_const, "printf 必须有正确内建签名");
    auto redeclaration = declaration("printf", 90, SymbolKind::Function, printf_entry->type);
    require(table.insert(redeclaration) == printf_id &&
            table.symbol(printf_id)->builtin == BuiltinKind::Printf, "兼容原型不能丢掉内建标记");
    redeclaration.is_defined = true;
    require(!table.insert(redeclaration), "用户定义不能覆盖内建函数");
    const auto global_x = table.insert(declaration("x", 10)).value();
    const auto alpha = table.insert(declaration("alpha", 11)).value();
    const auto scope = table.enter_scope(ScopeKind::Function, at(20));
    require(!register_builtins(table), "内建不能登记到函数作用域");
    const auto local_x = table.insert(declaration("x", 50)).value();
    table.insert(declaration("alpine", 30));
    const auto matches = table.prefix_query("al");
    require(matches.size() == 2 && matches[0] == alpha &&
            table.symbol(matches[1])->name == "alpine", "补全结果应按名字排序");
    require(table.prefix_query("x") == std::vector<SymbolId>{local_x}, "当前补全应去掉被遮蔽名字");
    require(table.prefix_query("x", scope, {3, 5, 40}) == std::vector<SymbolId>{global_x},
            "光标前尚未声明的内层名字不能遮蔽外层");
    require(table.prefix_query("x", scope, {3, 5, 50}) == std::vector<SymbolId>{local_x},
            "到声明位置后内层应遮蔽外层");
    require(table.prefix_query("pri", scope, {1, 1, 0}) == std::vector<SymbolId>{printf_id},
            "内建应始终可见");
    table.exit_scope();
    require(table.prefix_query("x", scope, {3, 5, 60}) == std::vector<SymbolId>{local_x} &&
            table.prefix_query("", invalid_id, {1, 1, 0}).empty(),
            "持久作用域补全应支持退出后的查询及非法编号");
    SymbolTable conflict(diagnostics);
    conflict.insert(declaration("scanf"));
    require(!register_builtins(conflict) && conflict.data().symbols.size() == 1 &&
            !conflict.lookup("printf"), "内建冲突不应只登记一半");
    SymbolTable compatible(diagnostics);
    auto compatible_declaration = declaration("printf", 80, SymbolKind::Function,
                                              table.symbol(printf_id)->type);
    const auto compatible_id = compatible.insert(compatible_declaration).value();
    require(register_builtins(compatible) && compatible.lookup("printf") == compatible_id &&
            compatible.symbol(compatible_id)->builtin == BuiltinKind::Printf &&
            compatible.symbol(compatible_id)->range.begin.offset == 80,
            "兼容的已有原型应复用编号和首次位置，并补上内建标记");
    SymbolTable user_definition(diagnostics);
    compatible_declaration.is_defined = true;
    user_definition.insert(compatible_declaration);
    require(!register_builtins(user_definition) && user_definition.data().symbols.size() == 1 &&
            user_definition.symbol(0)->is_defined && user_definition.symbol(0)->builtin == BuiltinKind::None,
            "内建登记不能覆盖用户已有函数定义");
}

int main() {
    try {
        test_diagnostics();
        test_types();
        test_scopes_and_functions();
        test_records();
        test_builtins_and_completion();
        std::cout << "符号表、诊断与类型规则：5 组测试全部通过\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "测试失败：" << error.what() << '\n';
        return 1;
    }
}
