#include "minic/interface.hpp"

#include <iostream>

using namespace minic;

int main() {
    NodeArena arena;
    ASTNode* root = arena.make();
    ASTNode* first = arena.make();
    ASTNode* second = arena.make();
    root->kind = NodeType::Program;
    first->kind = NodeType::Identifier;
    second->kind = NodeType::IntLiteral;
    root->children.push_back(first);
    root->children.push_back(second);
    if (root->children.size() != 2 || root->children[0] != first || root->children[1] != second) {
        std::cerr << "AST 子节点顺序错误\n";
        return 1;
    }
    NodeArena moved = std::move(arena);
    if (moved.nodes.size() != 3 || arena.nodes.size() != 0 || moved.nodes[0] != root) {
        std::cerr << "Arena 移动后所有权错误\n";
        return 1;
    }
    ConstantValue empty;
    ConstantValue integer(static_cast<long long>(7));
    if (!constant_empty(empty) || constant_empty(integer) || !constant_get_if<long long>(integer) ||
        *constant_get_if<long long>(integer) != 7) {
        std::cerr << "常量显式状态错误\n";
        return 1;
    }
    return 0;
}
