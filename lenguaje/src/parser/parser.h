// Parser descendente recursivo: tokens -> AST (ast/ast.h).
#pragma once

#include <memory>
#include <string>

#include "ast/ast.h"
#include "common/diagnostics.h"

namespace lg {

// Analiza `source` (contenido de `file`). Los errores se registran en `diags`.
// Ante errores devuelve un Program parcial y diags.hasErrors() será true.
// Nunca devuelve nullptr. Registra el texto fuente en `diags` para poder mostrar el caret.
std::unique_ptr<Program> parseProgram(const std::string& file, const std::string& source, Diagnostics& diags);

}  // namespace lg
