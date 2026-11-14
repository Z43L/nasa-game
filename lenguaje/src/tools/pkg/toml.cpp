#include "toml.h"

#include <cctype>
#include <set>

namespace lgpm {
namespace {

struct Cursor {
  const std::string& s;
  size_t i = 0;
  int line = 1;

  [[noreturn]] void fail(const std::string& msg) const {
    throw TomlError("línea " + std::to_string(line) + ": " + msg);
  }
  bool eof() const { return i >= s.size(); }
  char peek() const { return eof() ? '\0' : s[i]; }
  void ws() {
    while (!eof() && (s[i] == ' ' || s[i] == '\t')) ++i;
  }
  // Tras un valor solo puede haber espacios y, opcionalmente, un comentario.
  void endOfLine() {
    ws();
    if (!eof() && s[i] != '#') fail("texto sobrante después del valor");
  }
};

bool isBareChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
}

std::string parseString(Cursor& c) {
  ++c.i;  // comilla de apertura
  std::string out;
  while (!c.eof() && c.s[c.i] != '"') {
    char ch = c.s[c.i];
    if (ch == '\\') {
      ++c.i;
      if (c.eof()) c.fail("cadena sin cerrar");
      switch (c.s[c.i]) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case 'n': out += '\n'; break;
        case 't': out += '\t'; break;
        default: c.fail(std::string("escape no válido en cadena: \\") + c.s[c.i]);
      }
    } else {
      out += ch;
    }
    ++c.i;
  }
  if (c.eof()) c.fail("cadena sin cerrar");
  ++c.i;  // comilla de cierre
  return out;
}

std::string parseKey(Cursor& c) {
  c.ws();
  if (c.peek() == '"') return parseString(c);
  size_t start = c.i;
  while (!c.eof() && isBareChar(c.s[c.i])) ++c.i;
  if (c.i == start) c.fail("se esperaba una clave (letras, cifras, '_' o '-')");
  return c.s.substr(start, c.i - start);
}

TomlValue parseValue(Cursor& c) {
  c.ws();
  TomlValue v;
  if (c.peek() == '"') {
    v.kind = TomlValue::Kind::String;
    v.str = parseString(c);
    return v;
  }
  if (c.peek() == '{') {
    ++c.i;
    v.kind = TomlValue::Kind::Table;
    c.ws();
    if (c.peek() == '}') {
      ++c.i;
      return v;
    }
    while (true) {
      std::string key = parseKey(c);
      c.ws();
      if (c.peek() != '=') c.fail("se esperaba '=' después de la clave '" + key + "'");
      ++c.i;
      if (v.table.count(key)) c.fail("clave repetida en tabla en línea: '" + key + "'");
      v.table[key] = parseValue(c);
      c.ws();
      if (c.peek() == ',') {
        ++c.i;
        continue;
      }
      if (c.peek() == '}') {
        ++c.i;
        return v;
      }
      c.fail("se esperaba ',' o '}' en tabla en línea");
    }
  }
  c.fail("valor no soportado: usa una cadena \"...\" o una tabla { ... }");
}

}  // namespace

TomlTable parseToml(const std::string& text) {
  TomlTable root;
  TomlTable* current = &root;
  std::set<std::string> headers;

  Cursor c{text};
  size_t pos = 0;
  while (pos <= text.size()) {
    size_t nl = text.find('\n', pos);
    std::string lineText = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
    if (!lineText.empty() && lineText.back() == '\r') lineText.pop_back();
    Cursor lc{lineText};
    lc.line = c.line;

    lc.ws();
    if (!lc.eof() && lc.peek() != '#') {
      if (lc.peek() == '[') {
        ++lc.i;
        std::string name = parseKey(lc);
        lc.ws();
        if (lc.peek() != ']') lc.fail("se esperaba ']' al final de la cabecera de tabla");
        ++lc.i;
        lc.endOfLine();
        if (headers.count(name)) lc.fail("tabla repetida: [" + name + "]");
        headers.insert(name);
        auto found = root.find(name);
        if (found != root.end() && found->second.kind == TomlValue::Kind::String)
          lc.fail("'" + name + "' ya es un valor, no una tabla");
        TomlValue& slot = root[name];
        slot.kind = TomlValue::Kind::Table;
        current = &slot.table;
      } else {
        std::string key = parseKey(lc);
        lc.ws();
        if (lc.peek() != '=') lc.fail("se esperaba '=' después de la clave '" + key + "'");
        ++lc.i;
        if (current->count(key)) lc.fail("clave repetida: '" + key + "'");
        TomlValue value = parseValue(lc);
        lc.endOfLine();
        (*current)[key] = std::move(value);
      }
    }

    if (nl == std::string::npos) break;
    pos = nl + 1;
    c.line++;
  }
  return root;
}

std::string tomlQuote(const std::string& s) {
  std::string out = "\"";
  for (char ch : s) {
    switch (ch) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      default: out += ch;
    }
  }
  return out + "\"";
}

}  // namespace lgpm
