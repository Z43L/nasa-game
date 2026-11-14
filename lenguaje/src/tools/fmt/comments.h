// Recolector de comentarios para lgfmt.
// El lexer descarta los comentarios ('#' hasta el fin de línea), así que se leen del texto fuente.
// No se modifica el lexer: este recolector es independiente y solo mira caracteres.
#pragma once

#include <string>
#include <vector>

namespace lgfmt {

struct Comment {
  int line = 0;           // línea 1-based
  int col = 0;            // columna 1-based del '#'
  bool trailing = false;  // true si hay código antes del '#' en la misma línea
  std::string text;       // desde '#' hasta el fin de línea, sin espacios finales
};

// Todos los comentarios del texto, en orden de aparición. Ignora '#' dentro de cadenas.
std::vector<Comment> collectComments(const std::string& source);

// Líneas del texto sin '\n' ni '\r' final. La línea k (1-based) es lines[k - 1].
std::vector<std::string> splitLines(const std::string& source);

}  // namespace lgfmt
