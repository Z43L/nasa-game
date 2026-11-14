#include "lexer/lexer.h"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace lg {
namespace {

const std::unordered_map<std::string, TokenKind>& keywords() {
  static const std::unordered_map<std::string, TokenKind> table = {
      {"fn", TokenKind::KwFn},         {"mut", TokenKind::KwMut},
      {"if", TokenKind::KwIf},         {"elif", TokenKind::KwElif},
      {"else", TokenKind::KwElse},     {"for", TokenKind::KwFor},
      {"in", TokenKind::KwIn},         {"while", TokenKind::KwWhile},
      {"return", TokenKind::KwReturn}, {"struct", TokenKind::KwStruct},
      {"impl", TokenKind::KwImpl},     {"extern", TokenKind::KwExtern},
      {"import", TokenKind::KwImport}, {"true", TokenKind::KwTrue},
      {"false", TokenKind::KwFalse},   {"break", TokenKind::KwBreak},
      {"continue", TokenKind::KwContinue}, {"and", TokenKind::KwAnd},
      {"or", TokenKind::KwOr},         {"not", TokenKind::KwNot},
      {"hilo", TokenKind::KwHilo},     {"async", TokenKind::KwAsync},
      {"await", TokenKind::KwAwait},
  };
  return table;
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isIdentStart(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
bool isIdentPart(char c) { return isIdentStart(c) || isDigit(c); }
bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

// Valor de un dígito en base 16 (o -1). Para 0x, 0b y 0o solo interesa el valor.
int digitValue(char c) {
  if (isDigit(c)) return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Forma válida de un literal decimal: dígitos [ '.' dígitos ] [ (e|E) [+|-] dígitos ].
bool isWellFormedDecimal(const std::string& t) {
  size_t i = 0;
  const size_t n = t.size();
  auto digits = [&] {
    const size_t s = i;
    while (i < n && isDigit(t[i])) ++i;
    return i > s;
  };
  if (!digits()) return false;
  if (i < n && t[i] == '.') {
    ++i;
    if (!digits()) return false;
  }
  if (i < n && (t[i] == 'e' || t[i] == 'E')) {
    ++i;
    if (i < n && (t[i] == '+' || t[i] == '-')) ++i;
    if (!digits()) return false;
  }
  return i == n;
}

// Sugerencia para un literal numérico malformado (el texto completo ya está leído).
std::string numberHint(const std::string& t) {
  // Prefijos de otra base: 0x10, 0b101, 0o17.
  if (t.size() > 2 && t[0] == '0' && std::string("xXbBoO").find(t[1]) != std::string::npos) {
    const int base = (t[1] == 'x' || t[1] == 'X') ? 16 : (t[1] == 'b' || t[1] == 'B') ? 2 : 8;
    const std::string prefix = t.substr(0, 2);
    unsigned long long value = 0;
    bool ok = true;
    for (size_t i = 2; i < t.size() && ok; ++i) {
      const int d = digitValue(t[i]);
      if (d < 0 || d >= base || value > (9223372036854775807ULL - d) / base) {
        ok = false;
      } else {
        value = value * base + d;
      }
    }
    if (ok) {
      return "los enteros no admiten prefijo " + prefix + "; escribe " + std::to_string(value) +
             " en decimal";
    }
    return "los enteros no admiten prefijo " + prefix + "; escribe el valor en decimal";
  }
  // Separadores de miles: 1_000 -> 1000.
  if (t.find('_') != std::string::npos) {
    std::string stripped;
    for (char c : t)
      if (c != '_') stripped += c;
    if (!stripped.empty() && isWellFormedDecimal(stripped)) {
      return "los separadores de miles no existen; escribe " + stripped;
    }
    return "los guiones bajos no separan dígitos; escribe el número sin '_'";
  }
  // Exponente sin dígitos: 1e, 1e+.
  const size_t e = t.find_last_of("eE");
  if (e != std::string::npos) {
    const std::string rest = t.substr(e + 1);
    if (rest.empty() || rest == "+" || rest == "-") {
      return "el exponente necesita dígitos; escribe, por ejemplo, 1e3";
    }
  }
  if (std::count(t.begin(), t.end(), '.') > 1) {
    return "un número decimal tiene un solo punto; escribe, por ejemplo, 1.25";
  }
  if (t.back() == '.') return "un decimal necesita dígitos después del punto; escribe, por ejemplo, 1.0";
  for (char c : t) {
    if (isAlpha(c)) {
      return "los números no pueden llevar letras pegadas; sepáralos de la palabra con un espacio";
    }
  }
  return "escribe el número con dígitos, por ejemplo 12, 1.5 o 1e3";
}

class Lexer {
 public:
  Lexer(const std::string& file, const std::string& src, Diagnostics& diags)
      : file_(file), src_(src), diags_(diags) {
    indents_.push_back(0);
  }

  std::vector<Token> run() {
    // BOM UTF-8 al inicio del archivo: se ignora sin error ni cambio de columna.
    if (src_.size() >= 3 && static_cast<unsigned char>(src_[0]) == 0xEF &&
        static_cast<unsigned char>(src_[1]) == 0xBB && static_cast<unsigned char>(src_[2]) == 0xBF) {
      pos_ = 3;
    }
    while (!atEnd()) {
      if (lineStart_ && brackets_.empty()) {
        lineStart_ = false;
        lexIndentation();
        if (atEnd()) break;
      }
      lexOne();
    }
    finish();
    return std::move(tokens_);
  }

 private:
  struct OpenBracket {
    char ch;
    int line;
    int col;
  };

  bool atEnd() const { return pos_ >= src_.size(); }
  char at(size_t off = 0) const {
    return pos_ + off < src_.size() ? src_[pos_ + off] : '\0';
  }
  void advance() {
    if (src_[pos_] == '\n') {
      ++line_;
      col_ = 1;
    } else {
      ++col_;
    }
    ++pos_;
  }

  void error(int line, int col, std::string msg, std::string hint) {
    diags_.error(Loc{file_, line, col}, std::move(msg), std::move(hint));
  }

  // Añade un token. Los tokens de contenido marcan la línea como no vacía.
  Token& push(TokenKind kind, int line, int col) {
    tokens_.emplace_back();
    Token& t = tokens_.back();
    t.kind = kind;
    t.line = line;
    t.col = col;
    if (kind != TokenKind::Newline && kind != TokenKind::Indent && kind != TokenKind::Dedent) {
      lineHasContent_ = true;
    }
    return t;
  }

  // Token de uno o dos caracteres cuyo lexema es el texto fuente.
  void simple(TokenKind kind, size_t len, int line, int col) {
    push(kind, line, col).text = src_.substr(pos_, len);
    for (size_t i = 0; i < len; ++i) advance();
  }

  // Al inicio de una línea física (fuera de paréntesis): mide la indentación y emite
  // INDENT/DEDENT. Las líneas vacías o de solo comentario no cambian la indentación.
  void lexIndentation() {
    int indent = 0;
    while (!atEnd()) {
      const char c = at();
      if (c == ' ') {
        ++indent;
        advance();
      } else if (c == '\t') {
        error(line_, col_, "tabulación no permitida",
              "usa espacios para indentar; el lenguaje no admite tabuladores");
        advance();
      } else if (c == '\r') {
        advance();
      } else {
        break;
      }
    }
    if (atEnd() || at() == '\n' || at() == '#') return;  // línea vacía o solo comentario

    const int line = line_, col = col_;
    if (indent > indents_.back()) {
      indents_.push_back(indent);
      push(TokenKind::Indent, line, col);
      return;
    }
    while (indent < indents_.back()) {
      indents_.pop_back();
      push(TokenKind::Dedent, line, col);
    }
    if (indent != indents_.back()) {
      error(line, col, "indentación inconsistente",
            "esta línea no coincide con ningún nivel de indentación anterior; alinéala con un bloque abierto");
    }
  }

  void lexOne() {
    const char c = at();
    const int line = line_, col = col_;
    if (c == ' ' || c == '\r') {
      advance();
      return;
    }
    if (c == '\t') {
      error(line, col, "tabulación no permitida",
            "usa espacios para indentar; el lenguaje no admite tabuladores");
      advance();
      return;
    }
    if (c == '#') {
      while (!atEnd() && at() != '\n') advance();
      return;
    }
    if (c == '\n') {
      advance();
      if (brackets_.empty()) {
        if (lineHasContent_) push(TokenKind::Newline, line, col);
        lineHasContent_ = false;
        lineStart_ = true;
      }
      return;
    }
    if (isIdentStart(c)) {
      lexWord(line, col);
      return;
    }
    if (isDigit(c)) {
      lexNumber(line, col);
      return;
    }
    if (c == '"') {
      lexString(line, col);
      return;
    }
    lexOperator(line, col);
  }

  void lexWord(int line, int col) {
    const size_t start = pos_;
    while (!atEnd() && isIdentPart(at())) advance();
    std::string word = src_.substr(start, pos_ - start);
    auto it = keywords().find(word);
    const TokenKind kind = it == keywords().end() ? TokenKind::Ident : it->second;
    push(kind, line, col).text = std::move(word);
  }

  void lexNumber(int line, int col) {
    const size_t start = pos_;
    // Se lee el literal completo: dígitos, letras, '_', '.' (pero no el '..' de un rango)
    // y el signo que sigue a e/E. Así "0x10", "1_000" o "3abc" dan un único error claro.
    while (!atEnd()) {
      const char c = at();
      if (isIdentPart(c)) {
        advance();
      } else if (c == '.' && at(1) != '.') {
        advance();
      } else if ((c == '+' || c == '-') && pos_ > start && (src_[pos_ - 1] == 'e' || src_[pos_ - 1] == 'E')) {
        advance();
      } else {
        break;
      }
    }
    const std::string text = src_.substr(start, pos_ - start);
    const bool wellFormed = isWellFormedDecimal(text);
    if (!wellFormed) {
      error(line, col, "literal numérico no válido '" + text + "'", numberHint(text));
      // Se emite un entero de relleno para que el parser siga sin error en cascada.
      Token& t = push(TokenKind::Int, line, col);
      t.text = text;
      return;
    }
    const bool isFloat = text.find_first_of(".eE") != std::string::npos;
    if (isFloat) {
      Token& t = push(TokenKind::Float, line, col);
      t.text = text;
      t.floatValue = std::strtod(text.c_str(), nullptr);
      return;
    }
    Token& t = push(TokenKind::Int, line, col);
    t.text = text;
    try {
      t.intValue = std::stoll(text);
    } catch (const std::out_of_range&) {
      error(line, col, "entero demasiado grande: " + text,
            "el máximo para int es 9223372036854775807");
    }
  }

  void lexString(int line, int col) {
    advance();  // comilla de apertura
    std::string value;
    bool closed = false;
    while (!atEnd()) {
      const char c = at();
      if (c == '\n') break;
      if (c == '"') {
        advance();
        closed = true;
        break;
      }
      if (c == '\\') {
        const int escLine = line_, escCol = col_;
        advance();
        if (atEnd() || at() == '\n') break;
        switch (at()) {
          case 'n': value += '\n'; break;
          case 'r': value += '\r'; break;
          case 't': value += '\t'; break;
          case '\\': value += '\\'; break;
          case '"': value += '"'; break;
          default:
            error(escLine, escCol,
                  std::string("secuencia de escape desconocida '\\") + at() + "'",
                  "escapes válidos: \\n \\r \\t \\\\ \\\"");
            break;
        }
        advance();
        continue;
      }
      value += c;
      advance();
    }
    if (!closed) {
      error(line, col, "cadena sin cerrar", "cierra la cadena con \" antes del fin de línea");
    }
    push(TokenKind::String, line, col).text = std::move(value);
  }

  void openBracket(TokenKind kind, int line, int col) {
    brackets_.push_back(OpenBracket{at(), line, col});
    simple(kind, 1, line, col);
  }

  void closeBracket(char closer, char opener, TokenKind kind, int line, int col) {
    if (brackets_.empty()) {
      error(line, col, std::string("'") + closer + "' sin '" + opener + "' correspondiente",
            "elimina este cierre o abre el paréntesis antes");
    } else {
      const OpenBracket& top = brackets_.back();
      if (top.ch != opener) {
        error(line, col,
              std::string("'") + closer + "' no coincide con '" + top.ch + "' abierto en la línea " +
                  std::to_string(top.line),
              "cierra primero el paréntesis abierto");
      }
      brackets_.pop_back();
    }
    simple(kind, 1, line, col);
  }

  void lexOperator(int line, int col) {
    const char c = at();
    const char n = at(1);
    switch (c) {
      case '(': openBracket(TokenKind::LParen, line, col); return;
      case '[': openBracket(TokenKind::LBracket, line, col); return;
      case '{': openBracket(TokenKind::LBrace, line, col); return;
      case ')': closeBracket(')', '(', TokenKind::RParen, line, col); return;
      case ']': closeBracket(']', '[', TokenKind::RBracket, line, col); return;
      case '}': closeBracket('}', '{', TokenKind::RBrace, line, col); return;
      case ',': simple(TokenKind::Comma, 1, line, col); return;
      case ':': simple(TokenKind::Colon, 1, line, col); return;
      case '+': simple(TokenKind::Plus, 1, line, col); return;
      case '*': simple(TokenKind::Star, 1, line, col); return;
      case '/': simple(TokenKind::Slash, 1, line, col); return;
      case '%': simple(TokenKind::Percent, 1, line, col); return;
      case '@': simple(TokenKind::At, 1, line, col); return;
      case '-':
        if (n == '>') simple(TokenKind::Arrow, 2, line, col);
        else simple(TokenKind::Minus, 1, line, col);
        return;
      case '.':
        if (n == '.') simple(TokenKind::DotDot, 2, line, col);
        else simple(TokenKind::Dot, 1, line, col);
        return;
      case '=':
        if (n == '=') simple(TokenKind::EqEq, 2, line, col);
        else simple(TokenKind::Assign, 1, line, col);
        return;
      case '<':
        if (n == '=') simple(TokenKind::Le, 2, line, col);
        else simple(TokenKind::Lt, 1, line, col);
        return;
      case '>':
        if (n == '=') simple(TokenKind::Ge, 2, line, col);
        else simple(TokenKind::Gt, 1, line, col);
        return;
      case '!':
        if (n == '=') {
          simple(TokenKind::NotEq, 2, line, col);
          return;
        }
        error(line, col, "carácter inesperado '!'", "para negar usa 'not'; para distinto usa '!='");
        advance();
        return;
      case ';':
        error(line, col, "carácter inesperado ';'",
              "el lenguaje no usa punto y coma: termina cada sentencia con un salto de línea");
        advance();
        return;
      default:
        break;
    }
    const unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 0x80) {  // texto no ASCII (p. ej. UTF-8 fuera de cadenas): se reporta una vez
      while (!atEnd() && static_cast<unsigned char>(at()) >= 0x80) advance();
      error(line, col, "carácter no válido fuera de una cadena",
            "el código fuente solo admite ASCII fuera de las cadenas y comentarios");
      return;
    }
    if (uc < 0x20) {
      error(line, col, "carácter de control no válido", "elimina este carácter invisible");
    } else {
      error(line, col, std::string("carácter inesperado '") + c + "'",
            "este carácter no forma parte del lenguaje");
    }
    advance();
  }

  // Cierra la última línea y los bloques abiertos; añade el End.
  void finish() {
    if (lineHasContent_) push(TokenKind::Newline, line_, col_);
    lineHasContent_ = false;
    while (indents_.size() > 1) {
      indents_.pop_back();
      push(TokenKind::Dedent, line_, col_);
    }
    if (!brackets_.empty()) {
      const OpenBracket& o = brackets_.back();
      std::string closer = o.ch == '(' ? ")" : o.ch == '[' ? "]" : "}";
      error(o.line, o.col, std::string("'") + o.ch + "' sin cerrar",
            "añade '" + closer + "' para cerrar el paréntesis");
    }
    push(TokenKind::End, line_, col_);
  }

  const std::string& file_;
  const std::string& src_;
  Diagnostics& diags_;
  std::vector<Token> tokens_;
  std::vector<int> indents_;
  std::vector<OpenBracket> brackets_;
  size_t pos_ = 0;
  int line_ = 1;
  int col_ = 1;
  bool lineStart_ = true;
  bool lineHasContent_ = false;
};

}  // namespace

std::vector<Token> tokenize(const std::string& file, const std::string& source, Diagnostics& diags) {
  return Lexer(file, source, diags).run();
}

const char* tokenKindName(TokenKind kind) {
  struct Entry {
    TokenKind kind;
    const char* name;
  };
  static const Entry kNames[] = {
      {TokenKind::Ident, "identificador"}, {TokenKind::Int, "número entero"},
      {TokenKind::Float, "número decimal"}, {TokenKind::String, "cadena"},
      {TokenKind::KwFn, "'fn'"}, {TokenKind::KwMut, "'mut'"}, {TokenKind::KwIf, "'if'"},
      {TokenKind::KwElif, "'elif'"}, {TokenKind::KwElse, "'else'"}, {TokenKind::KwFor, "'for'"},
      {TokenKind::KwIn, "'in'"}, {TokenKind::KwWhile, "'while'"}, {TokenKind::KwReturn, "'return'"},
      {TokenKind::KwStruct, "'struct'"}, {TokenKind::KwImpl, "'impl'"},
      {TokenKind::KwExtern, "'extern'"}, {TokenKind::KwImport, "'import'"},
      {TokenKind::KwTrue, "'true'"}, {TokenKind::KwFalse, "'false'"},
      {TokenKind::KwBreak, "'break'"}, {TokenKind::KwContinue, "'continue'"},
      {TokenKind::KwAnd, "'and'"}, {TokenKind::KwOr, "'or'"}, {TokenKind::KwNot, "'not'"},
      {TokenKind::KwHilo, "'hilo'"}, {TokenKind::KwAsync, "'async'"}, {TokenKind::KwAwait, "'await'"},
      {TokenKind::LParen, "'('"}, {TokenKind::RParen, "')'"}, {TokenKind::LBracket, "'['"},
      {TokenKind::RBracket, "']'"}, {TokenKind::LBrace, "'{'"}, {TokenKind::RBrace, "'}'"},
      {TokenKind::Comma, "','"}, {TokenKind::Colon, "':'"}, {TokenKind::Dot, "'.'"},
      {TokenKind::DotDot, "'..'"}, {TokenKind::Arrow, "'->'"}, {TokenKind::Assign, "'='"},
      {TokenKind::EqEq, "'=='"}, {TokenKind::NotEq, "'!='"}, {TokenKind::Lt, "'<'"},
      {TokenKind::Gt, "'>'"}, {TokenKind::Le, "'<='"}, {TokenKind::Ge, "'>='"},
      {TokenKind::Plus, "'+'"}, {TokenKind::Minus, "'-'"}, {TokenKind::Star, "'*'"},
      {TokenKind::Slash, "'/'"}, {TokenKind::Percent, "'%'"}, {TokenKind::At, "'@'"},
      {TokenKind::Newline, "fin de línea"}, {TokenKind::Indent, "indentación"},
      {TokenKind::Dedent, "fin de bloque"}, {TokenKind::End, "fin del archivo"},
  };
  for (const Entry& e : kNames) {
    if (e.kind == kind) return e.name;
  }
  return "token desconocido";
}

}  // namespace lg
