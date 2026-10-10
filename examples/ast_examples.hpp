#pragma once
#include "minic/interface.hpp"

#include <utility>

// 手动组装 AST 的示例工具，供词法/语法完成前测试后续模块。
namespace minic::examples {

using Node = std::unique_ptr<ASTNode>;

inline TypePtr basic(TypeKind kind) {
    auto type = make_type_info();
    type->kind = kind;
    return type;
}

inline Node node(NodeType kind, const std::string& name = "") {
    auto value = std::make_unique<ASTNode>();
    value->kind = kind;
    value->name = name;
    value->range = {"ast-demo.c", {1, 1, 0}, {1, 2, 1}};
    return value;
}

template <typename... Children>
Node tree(NodeType kind, const std::string& name, Children... children) {
    auto value = node(kind, name);
    (value->children.push_back(std::move(children)), ...);
    return value;
}

inline Node id(const std::string& name) { return node(NodeType::Identifier, name); }
inline Node integer(int value) {
    if (value < 0) return tree(NodeType::UnaryOp, "-", node(NodeType::IntLiteral, std::to_string(-static_cast<std::int64_t>(value))));
    return node(NodeType::IntLiteral, std::to_string(value));
}
inline Node floating(const std::string& value) { return node(NodeType::FloatLiteral, value); }
inline Node text(const std::string& spelling) { return node(NodeType::StringLiteral, spelling); }
inline Node binary(const std::string& op, Node left, Node right) {
    return tree(NodeType::BinaryOp, op, std::move(left), std::move(right));
}
inline Node assign(const std::string& name, Node source, const std::string& op = "=") {
    return tree(NodeType::Assign, op, id(name), std::move(source));
}
inline Node statement(Node expression) { return tree(NodeType::ExprStmt, "", std::move(expression)); }
inline Node ret(Node expression) { return tree(NodeType::Return, "", std::move(expression)); }
inline Node variable(const std::string& name, TypeKind kind, Node init = {}) {
    auto value = node(NodeType::VarDecl, name);
    value->declared_type = basic(kind);
    if (init) value->children.push_back(std::move(init));
    return value;
}
inline Node parameter(const std::string& name, TypeKind kind) {
    auto value = node(NodeType::ParamDecl, name);
    value->declared_type = basic(kind);
    return value;
}
template <typename... Children>
Node block(Children... children) { return tree(NodeType::Block, "", std::move(children)...); }
template <typename... Arguments>
Node call(const std::string& name, Arguments... arguments) {
    return tree(NodeType::Call, name, id(name), std::move(arguments)...);
}
template <typename... Parameters>
Node function(const std::string& name, TypeKind result, Node body, Parameters... parameters) {
    auto value = tree(NodeType::FunctionDef, name, std::move(parameters)...);
    auto type = make_type_info();
    type->kind = TypeKind::Function;
    type->base = basic(result);
    for (const auto& entry : value->children) type->params.push_back(entry->declared_type);
    value->declared_type = type;
    value->children.push_back(std::move(body));
    return value;
}
template <typename... Declarations>
Node program(Declarations... declarations) { return tree(NodeType::Program, "", std::move(declarations)...); }

// 等价于读取 n，累计 1 到 n，输出结果并返回 0。
inline Node loop_program() {
    return program(function("main", TypeKind::Int, block(
        variable("n", TypeKind::Int),
        variable("sum", TypeKind::Int, integer(0)),
        statement(call("scanf", text("\"%d\""), tree(NodeType::UnaryOp, "&", id("n")))),
        tree(NodeType::For, "",
            variable("i", TypeKind::Int, integer(1)),
            binary("<=", id("i"), id("n")),
            tree(NodeType::UnaryOp, "post++", id("i")),
            block(statement(assign("sum", id("i"), "+=")))),
        statement(call("printf", text("\"sum = %d\\n\""), id("sum"))),
        ret(integer(0)))));
}

inline TypePtr array(TypePtr element, std::optional<std::size_t> length) {
    auto value = make_type_info();
    value->kind = TypeKind::Array; value->base = std::move(element); value->array_length = length;
    return value;
}
inline TypePtr record_type(const std::string& tag) {
    auto value = make_type_info(); value->kind = TypeKind::Struct; value->name = tag; return value;
}
inline Node object(const std::string& name, TypePtr type, Node init = {}) {
    auto value = node(NodeType::VarDecl, name); value->declared_type = std::move(type);
    if (init) value->children.push_back(std::move(init));
    return value;
}
inline Node member(const std::string& name, TypePtr type) {
    auto value = node(NodeType::MemberDecl, name); value->declared_type = std::move(type); return value;
}
inline Node index(Node base, Node subscript) { return tree(NodeType::ArrayAccess, "", std::move(base), std::move(subscript)); }
inline Node field(Node base, const std::string& name) { return tree(NodeType::MemberAccess, name, std::move(base)); }
inline Node set(Node destination, Node source, const std::string& op = "=") {
    return tree(NodeType::Assign, op, std::move(destination), std::move(source));
}
template <typename... Elements>
Node list(Elements... elements) { return tree(NodeType::InitList, "", std::move(elements)...); }

// 数组元素赋值、结构体成员求和及带填充的结构体布局示例。
inline Node aggregate_program() {
    auto structure = tree(NodeType::StructDef, "Result", member("label", basic(TypeKind::Char)), member("total", basic(TypeKind::Int)));
    return program(std::move(structure), function("main", TypeKind::Int, block(
        object("values", array(basic(TypeKind::Int), 3), list(integer(2), integer(3), integer(4))),
        object("result", record_type("Result"), list(node(NodeType::CharLiteral, "'S'"), integer(0))),
        tree(NodeType::For, "", variable("i", TypeKind::Int, integer(0)), binary("<", id("i"), integer(3)),
            tree(NodeType::UnaryOp, "post++", id("i")),
            block(statement(set(field(id("result"), "total"), index(id("values"), id("i")), "+=")))),
        statement(call("printf", text("\"%c = %d\\n\""), field(id("result"), "label"), field(id("result"), "total"))),
        ret(integer(0)))));
}

}
