#pragma once
#include "minic/interface.hpp"

namespace minic {

// 将源码分成单词，返回单词表和词法诊断；filename 用于记录来源位置。
LexResult lex(const std::string& source,
              const std::string& filename = "<input>");

}
