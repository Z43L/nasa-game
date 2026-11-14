// formatSource: parsea, imprime y verifica el resultado antes de devolverlo.
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "comments.h"
#include "format.h"
#include "parser/parser.h"

namespace lgfmt {
namespace {

std::vector<std::string> commentTexts(const std::string& src) {
  std::vector<std::string> texts;
  for (const Comment& c : collectComments(src)) texts.push_back(c.text);
  std::sort(texts.begin(), texts.end());
  return texts;
}

}  // namespace

bool formatSource(const std::string& file, const std::string& source, std::string& out,
                  lg::Diagnostics& diags, std::string& internal) {
  internal.clear();
  std::unique_ptr<lg::Program> prog = lg::parseProgram(file, source, diags);
  if (diags.hasErrors()) return false;

  const std::string formatted = formatProgram(*prog, source);

  // Verificación: el resultado debe ser código válido con el mismo árbol, los mismos comentarios
  // y debe ser punto fijo del formateador.
  lg::Diagnostics check;
  std::unique_ptr<lg::Program> again = lg::parseProgram(file, formatted, check);
  if (check.hasErrors()) {
    internal = "el texto formateado no parsea";
    return false;
  }
  if (dumpProgram(*prog) != dumpProgram(*again)) {
    internal = "el formato cambia el AST";
    return false;
  }
  if (commentTexts(source) != commentTexts(formatted)) {
    internal = "el formato pierde o altera comentarios";
    return false;
  }
  if (formatProgram(*again, formatted) != formatted) {
    internal = "el formato no es idempotente";
    return false;
  }
  out = formatted;
  return true;
}

}  // namespace lgfmt
