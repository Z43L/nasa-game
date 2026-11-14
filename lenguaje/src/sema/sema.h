// Análisis semántico: resolución de nombres, comprobación de tipos y rellenado de los
// campos marcados "sema" del AST (ver ast.h).
#pragma once

#include "ast/ast.h"
#include "common/diagnostics.h"
#include "sema/types.h"

namespace lg {

// Devuelve true si no se ha producido ningún error nuevo durante el análisis.
bool analyze(Program& prog, TypeTable& types, Diagnostics& diags);

}  // namespace lg
