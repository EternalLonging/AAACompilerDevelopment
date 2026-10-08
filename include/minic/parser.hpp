#pragma once
#include "minic/interface.hpp"

namespace minic {

// 根据单词表建立语法树，返回树和诊断；输入须有末尾 EOF，失败时根为空。
ParseResult parse(const std::vector<Token>& tokens);

}
