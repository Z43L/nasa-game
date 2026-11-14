// Lexer del lenguaje: convierte el texto fuente en tokens con línea y columna (1-based).
// La indentación es por espacios: emite INDENT / DEDENT / NEWLINE. No hay punto y coma.
// Dentro de (), [] o {} los saltos de línea se ignoran (permite listas y llamadas multilínea).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/diagnostics.h"

namespace lg {

enum class TokenKind {
  // Literales e identificadores
  Ident, Int, Float, String,
  // Palabras reservadas
  KwFn, KwMut, KwIf, KwElif, KwElse, KwFor, KwIn, KwWhile, KwReturn, KwStruct, KwImpl,
  KwExtern, KwImport, KwTrue, KwFalse, KwBreak, KwContinue, KwAnd, KwOr, KwNot,
  KwHilo, KwAsync, KwAwait,  // concurrencia (v2)
  // Delimitadores y operadores
  LParen, RParen, LBracket, RBracket, LBrace, RBrace,
  Comma, Colon, Dot, DotDot, Arrow, Assign,
  EqEq, NotEq, Lt, Gt, Le, Ge,
  Plus, Minus, Star, Slash, Percent, At,
  // Estructura de línea y bloques
  Newline, Indent, Dedent, End
};

struct Token {
  TokenKind kind = TokenKind::End;
  std::string text;          // lexema; en String, el valor ya procesado (sin comillas ni escapes)
  int line = 1;              // 1-based
  int col = 1;               // 1-based
  int64_t intValue = 0;      // Int
  double floatValue = 0.0;   // Float
};

// Tokeniza `source`. Los errores léxicos se registran en `diags` y el lexer sigue
// con el resto del archivo. Siempre termina la secuencia con un token End.
std::vector<Token> tokenize(const std::string& file, const std::string& source, Diagnostics& diags);

// Nombre legible para mensajes de error, p. ej. "':'", "'fn'", "fin de línea".
const char* tokenKindName(TokenKind kind);

}  // namespace lg
