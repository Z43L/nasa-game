// Parser TOML mínimo para lg.toml y lg.lock.
// Soporta: comentarios (#), tablas [nombre], pares clave = valor, cadenas "..." (con \" \\ \n \t)
// y tablas en línea { clave = valor, ... }. No soporta arrays, enteros, booleanos, fechas
// ni tablas con puntos ([a.b]).
#pragma once

#include <map>
#include <stdexcept>
#include <string>

namespace lgpm {

struct TomlValue {
  enum class Kind { String, Table };
  Kind kind = Kind::String;
  std::string str;                       // si kind == String
  std::map<std::string, TomlValue> table;  // si kind == Table
};

using TomlTable = std::map<std::string, TomlValue>;

struct TomlError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// Lanza TomlError con el número de línea si el texto no es válido.
TomlTable parseToml(const std::string& text);

// Escapa una cadena para escribirla entre comillas dobles.
std::string tomlQuote(const std::string& s);

}  // namespace lgpm
