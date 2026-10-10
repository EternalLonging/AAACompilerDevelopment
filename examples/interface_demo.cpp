#include "minic/interface.hpp"
#include "minic/constant_pool.hpp"

#include <iostream>
#include <utility>

using namespace minic;

static TypePtr basic(TypeKind kind) {
    auto type = make_type_info();
    type->kind = kind;
    return type;
}

static TypePtr derived(TypeKind kind, TypePtr base,
                       std::optional<std::size_t> length = std::nullopt) {
    auto type = make_type_info();
    type->kind = kind;
    type->base = std::move(base);
    type->array_length = length;
    return type;
}

static std::unique_ptr<ASTNode> node(NodeType kind, std::string name,
                                     TypePtr type = {}) {
    auto result = std::make_unique<ASTNode>();
    result->kind = kind;
    result->name = std::move(name);
    result->type = std::move(type);
    result->range.file = "demo.c";
    result->scope_id = 1;
    return result;
}

int main() {
    minic::TypeArena types; // 手工类型及语法树借用的内存，保留到示例/测试结束。
    minic::TypeArenaScope type_scope(types);
    const auto int_type = basic(TypeKind::Int);
    const auto char_type = basic(TypeKind::Char);
    const auto char_pointer = derived(TypeKind::Address, char_type);
    auto printf_type = make_type_info();
    printf_type->kind = TypeKind::Function;
    printf_type->base = int_type;
    auto const_char = make_type_info();
    const_char->kind = TypeKind::Char;
    const_char->is_const = true;
    printf_type->params = {derived(TypeKind::Address, const_char)};
    printf_type->variadic = true;
    auto main_type = make_type_info();
    main_type->kind = TypeKind::Function;
    main_type->base = int_type;

    // All persistent IDs equal their indices. This demonstrates data assembly,
    // not symbol-table lookup or semantic-analysis implementation.
    SemanticResult semantic;
    auto& table = semantic.symbols;
    ScopeEntry global;
    global.id = 0;
    ScopeEntry function_scope;
    function_scope.id = 1;
    function_scope.parent = 0;
    function_scope.kind = ScopeKind::Function;
    ScopeEntry inner_scope;
    inner_scope.id = 2;
    inner_scope.parent = 1;
    inner_scope.kind = ScopeKind::Block;
    table.scopes = {global, function_scope, inner_scope};
    table.active_scopes = {0, 1, 2};

    SymbolEntry printf_symbol;
    printf_symbol.id = 0;
    printf_symbol.name = "printf";
    printf_symbol.kind = SymbolKind::Function;
    printf_symbol.type = printf_type;
    printf_symbol.scope = 0;
    printf_symbol.builtin = BuiltinKind::Printf;
    printf_symbol.is_defined = true;
    printf_symbol.range.file = "<builtin>";
    SymbolEntry main_symbol;
    main_symbol.id = 1;
    main_symbol.name = "main";
    main_symbol.kind = SymbolKind::Function;
    main_symbol.type = main_type;
    main_symbol.scope = 0;
    main_symbol.is_defined = true;
    SymbolEntry outer_s;
    outer_s.id = 2;
    outer_s.name = "s";
    outer_s.type = int_type;
    outer_s.scope = 1;
    outer_s.is_defined = true;
    SymbolEntry inner_s = outer_s;
    inner_s.id = 3;
    inner_s.scope = 2;
    table.symbols = {printf_symbol, main_symbol, outer_s, inner_s};
    table.scopes[0].symbols = {{"printf", 0}, {"main", 1}};
    table.scopes[1].symbols = {{"s", 2}};
    table.scopes[2].symbols = {{"s", 3}};
    table.active_scopes.pop_back(); // Inner records remain available to IR.
    table.active_scopes.pop_back();
    std::cout << "shadowed names: %s2 and %s3\n"
              << "after scope exit: " << table.symbols.size()
              << " symbols, " << table.scopes.size() << " scopes retained\n";

    // Example of an analyzed printf("%d", s) subtree.
    auto call = node(NodeType::Call, "printf", int_type);
    call->category = ValueCategory::RValue;
    auto callee = node(NodeType::Identifier, "printf", printf_type);
    callee->symbol_id = 0;
    callee->category = ValueCategory::Function;
    call->children.push_back(std::move(callee));
    auto literal = node(NodeType::StringLiteral, "\"%d\"",
                        derived(TypeKind::Array, char_type, 3));
    literal->value = std::string("%d");
    literal->category = ValueCategory::LValue;
    auto decay = node(NodeType::ImplicitCast, "", char_pointer);
    decay->category = ValueCategory::RValue;
    decay->children.push_back(std::move(literal));
    call->children.push_back(std::move(decay));
    auto s = node(NodeType::Identifier, "s", int_type);
    s->symbol_id = 2;
    s->category = ValueCategory::LValue;
    call->children.push_back(std::move(s));
    std::cout << "call arguments: " << call->children.size() - 1 << '\n';
    auto expr = node(NodeType::ExprStmt, "");
    expr->children.push_back(std::move(call));
    auto body = node(NodeType::Block, "");
    body->children.push_back(std::move(expr));
    auto function = node(NodeType::FunctionDef, "main", main_type);
    function->declared_type = main_type;
    function->symbol_id = 1;
    function->children.push_back(std::move(body));
    ParseResult parsed;
    parsed.root = node(NodeType::Program, "");
    parsed.root->scope_id = 0;
    parsed.root->children.push_back(std::move(function));

    // A multidimensional array is recursive, not a flattened length list.
    const auto matrix = derived(TypeKind::Array,
                                derived(TypeKind::Array, int_type, 4), 3);
    std::cout << "matrix dimensions: " << *matrix->array_length << " x "
              << *matrix->base->array_length << '\n';
    StructEntry record;
    record.id = 0;
    record.tag = "Pair";
    record.scope = 0;
    record.is_complete = true;
    record.size = 8;
    record.alignment = 4;
    record.members.push_back({"c", char_type, 0, {}});
    record.members.push_back({"x", int_type, 4, {}});
    table.records.push_back(std::move(record));
    table.scopes[0].tags.emplace("Pair", 0);
    auto record_type = make_type_info();
    record_type->kind = TypeKind::Struct;
    record_type->name = "Pair";
    record_type->record_id = 0;
    const auto record_address = derived(TypeKind::Address, record_type);
    std::cout << "internal record address target: " << record_address->base->name
              << ", x offset=" << *table.records[0].members[1].offset
              << ", size=" << *table.records[0].size << '\n';

    LexResult lexed;
    Token id;
    id.type = TokenType::ID;
    id.lexeme = "s";
    id.range = {"demo.c", {1, 1, 0}, {1, 2, 1}};
    Token eof;
    eof.col = 2;
    eof.range = {"demo.c", {1, 2, 1}, {1, 2, 1}};
    lexed.tokens = {id, eof};
    std::cout << "tokens including EOF: " << lexed.tokens.size() << '\n';

    IRResult ir;
    // 常量按类型和值去重，数值和格式串均通过 %c<ID> 在四元式中引用。
    ConstantPool pool;
    const auto five = pool.intern(int_type, std::int64_t{5}, "5");
    const auto format = pool.intern(derived(TypeKind::Array, char_type, 3),
                                    std::string("%d"), "\"%d\"");
    const auto zero = pool.intern(int_type, std::int64_t{0}, "0");
    ir.program.constants = std::move(pool).release();
    // 展示常量池编号与首次拼写，核对四元式 %c<ID> 所指向的条目。
    for (const auto& constant : ir.program.constants.entries) {
        std::cout << constant_operand(constant.id) << " = " << constant.spelling << '\n';
    }
    ir.program.entry_function = 1;
    IRFunction main_ir;
    main_ir.symbol_id = 1;
    main_ir.quads = {{"=", constant_operand(five), "-", "%s2"},
                     {"arg", constant_operand(format), "-", "-"},
                     {"arg", "%s2", "-", "-"},
                     {"call", "%s0", "2", "-"},
                     {"ret", constant_operand(zero), "-", "-"}};
    main_ir.locations.resize(main_ir.quads.size());
    ir.program.functions.push_back(std::move(main_ir));
    for (const auto& quad : ir.program.functions[0].quads) {
        std::cout << '(' << quad.op << ", " << quad.arg1 << ", "
                  << quad.arg2 << ", " << quad.result << ")\n";
    }
    // Demonstrate that an error stops the next stage even below the limit.
    Diagnostic error;
    error.phase = Phase::Lexer;
    error.code = "LEX_INVALID_CHAR";
    error.message = "invalid character";
    lexed.diagnostics.push_back(std::move(error));
    std::cout << "lexer with an error: " << (lexed.ok() ? "ok" : "stop")
              << "\nassembled AST/symbols/IR: "
              << (parsed.ok() && semantic.ok() && ir.ok() ? "ok" : "stop")
              << '\n';
}
