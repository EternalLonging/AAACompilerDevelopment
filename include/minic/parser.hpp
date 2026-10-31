#pragma once
#include "minic/interface.hpp"

namespace minic {

// 根据单词表建立语法树；首次出错立即停止，返回一条诊断和空根，输入须有末尾 EOF。
// 直接调用前建立 TypeArenaScope，类型管理器须保留到语法树用完。
ParseResult parse(const std::vector<Token>& tokens);

}
