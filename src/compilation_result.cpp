#include "minic/compiler.hpp"

namespace minic {

bool CompilationResult::ok() const {
    if (has_errors(diagnostics) || !lexical || !lexical->ok()) return false;
    if (target == CompileTarget::Tokens) return !syntax && !semantic && !ir;
    if (!syntax || !syntax->ok()) return false;
    if (target == CompileTarget::Parse) return !semantic && !ir;
    if (!semantic || !semantic->ok()) return false;
    if (target == CompileTarget::Check) return !ir;
    return target == CompileTarget::IR && ir && ir->ok();
}

}
