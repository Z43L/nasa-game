// Análisis para el servidor LSP: parsea y analiza un documento (con sus imports) y construye
// un índice propio de definiciones y referencias recorriendo el AST anotado por sema.
// No modifica ast.h: las posiciones de nombres se obtienen de los tokens del lexer.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ast/ast.h"
#include "common/diagnostics.h"
#include "lexer/lexer.h"
#include "sema/types.h"

namespace lsp {

struct FileText {
  std::string text;
  std::vector<lg::Token> tokens;  // ordenados por (línea, columna)
};

// Declaración con nombre: función, método, struct, variable, parámetro, campo o builtin.
struct Def {
  enum class Kind { Function, Method, Struct, Variable, Parameter, Field, Builtin };
  Kind kind = Kind::Variable;
  std::string name;
  std::string file;      // vacío para builtins
  int line = 0, col = 0; // posición del nombre (1-based)
  std::string detail;    // texto para hover, p. ej. "fn suma(a: int) -> int"
  const lg::Type* type = nullptr;  // tipo de variables, parámetros y campos
  std::string owner;     // struct a la que pertenece un método o campo
};

// Una aparición de un nombre en el código.
struct Ref {
  std::string file;
  int line = 0, col = 0;  // inicio del nombre (1-based)
  int len = 0;            // longitud en bytes del nombre
  int def = -1;           // índice en Snapshot::defs, o -1 si no se resuelve
  bool member = false;    // acceso con punto (obj.nombre): ver objLine/objCol
  int objLine = 0, objCol = 0;  // inicio de la expresión del objeto (para miembros)
  std::string name;
  std::string detail;     // texto extra para miembros builtin
  const lg::Type* objType = nullptr;  // tipo del objeto (miembros)
};

struct Snapshot {
  std::string mainFile;
  lg::Diagnostics diags;
  std::unique_ptr<lg::Program> prog;
  lg::TypeTable types;
  std::map<std::string, FileText> files;  // por ruta de archivo
  std::vector<Def> defs;
  std::vector<Ref> refs;
  std::map<std::string, int> identRefs;               // "file:line:col" -> índice en refs
  std::map<std::string, std::vector<int>> membersOf;  // struct -> defs (campos y métodos)
  bool parsed = false;   // false si hubo errores de sintaxis (no hay índice)
  bool analyzed = false; // true si sema terminó sin errores
};

// Construye el análisis del documento `mainFile` cuyo contenido es `mainText` (el buffer
// abierto en el editor). Los imports se leen del disco. Nunca devuelve nullptr.
std::unique_ptr<Snapshot> buildSnapshot(const std::string& mainFile, const std::string& mainText);

// Posición (1-based) del nombre de un token Ident en `s`, o nullptr.
// Busca la referencia de identificador en `file` en (line, col).
const Ref* identRefAt(const Snapshot& s, const std::string& file, int line, int col);

// Referencia de miembro (obj.nombre) cuyo nombre aparece en (line, col) del archivo.
const Ref* memberRefAt(const Snapshot& s, const std::string& file, const std::string& name, int line, int col);

// Nombre de la struct de un tipo struct (vacío si no lo es).
std::string structNameOf(const lg::Type* t);

// Listas para autocompletado.
const std::vector<std::string>& keywordNames();
const std::vector<std::string>& typeNames();
const std::vector<std::string>& builtinNames();

}  // namespace lsp
