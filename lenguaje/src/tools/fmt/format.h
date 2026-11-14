// API de lgfmt: impresor canónico del AST, volcado del AST y formateo verificado.
#pragma once

#include <string>

#include "ast/ast.h"
#include "common/diagnostics.h"

namespace lgfmt {

// Imprime el programa con formato canónico. `source` es el texto del que salió el AST: se usa
// para recuperar comentarios y líneas en blanco, que el AST no guarda.
std::string formatProgram(const lg::Program& prog, const std::string& source);

// Volcado canónico del AST sin posiciones. Sirve para comprobar que el formato no cambia el árbol.
std::string dumpProgram(const lg::Program& prog);

// Formatea `source` y verifica el resultado: debe parsear sin errores, dar el mismo volcado de AST,
// conservar todos los comentarios y ser idempotente. Devuelve false si hay errores de parseo
// (quedan en `diags`) o si falla la verificación (el motivo queda en `internal`).
bool formatSource(const std::string& file, const std::string& source, std::string& out,
                  lg::Diagnostics& diags, std::string& internal);

}  // namespace lgfmt
