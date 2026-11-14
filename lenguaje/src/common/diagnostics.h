// CONTRATO (propietario: integrador). No modificar sin avisar.
// Diagnósticos del compilador: errores con archivo, línea, columna, mensaje y sugerencia.
#pragma once

#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace lg {

// Posición en el código fuente.
struct Loc {
  std::string file;
  int line = 0;
  int col = 0;
};

struct Diagnostic {
  enum class Severity { Error, Warning, Note };
  Severity severity = Severity::Error;
  Loc loc;
  std::string message;  // qué salió mal: "tipo esperado int, recibido str"
  std::string hint;     // cómo corregirlo (opcional): "usa int(x) para convertir"
};

class Diagnostics {
public:
  // Guarda el texto de un archivo para poder mostrar la línea y el caret al imprimir.
  void registerSource(const std::string& file, const std::string& text);

  void error(const Loc& loc, std::string message, std::string hint = "");
  void warning(const Loc& loc, std::string message, std::string hint = "");

  bool hasErrors() const;
  size_t errorCount() const;
  const std::vector<Diagnostic>& all() const;

  // Formato:
  //   archivo.lg:3:5: error: mensaje
  //      3 | código fuente de la línea
  //        |     ^
  //   sugerencia: texto
  // Se imprimen como mucho 20 errores; después escribe "demasiados errores, se omiten N más"
  // (N = errores no impresos). errorCount() y hasErrors() siguen contando todos.
  void print(std::ostream& os) const;

private:
  std::vector<Diagnostic> items_;
  std::unordered_map<std::string, std::string> sources_;
};

}  // namespace lg
