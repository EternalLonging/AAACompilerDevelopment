#pragma once
#include "minic/interface.hpp"

namespace minic {

struct CompletionItem {
    std::string label; // 弹出列表中显示的名字。
    std::string kind; // 候选类别：符号、成员或关键字。
    std::string detail; // 类型或简短用途说明。
};

struct CompletionResult {
    std::size_t begin = 0; // 要替换的前缀开始字节位置。
    std::size_t end = 0; // 光标字节位置，不包含此位置。
    std::vector<CompletionItem> items; // 前缀匹配的候选，完全匹配优先，其次按作用域由近到远排列。
    bool recovered = false; // 是否修补未完成的输入后才获得符号信息。
};

// 按源码字节位置补全名字；优先解析完整源码，必要时修补光标前的片段。
// 不在注释或字符串内补全；无法解析时仍可提供上下文关键字。
CompletionResult complete(const std::string& source, std::size_t cursor);

}
