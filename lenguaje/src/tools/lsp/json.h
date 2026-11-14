// JSON mínimo (RFC 8259) para el servidor LSP: valor, parser y escritor. Sin dependencias.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace lsp {

class Json {
public:
  enum class Kind { Null, Bool, Number, String, Array, Object };

  Json() = default;  // null
  Json(bool b) : kind_(Kind::Bool), bool_(b) {}
  Json(int n) : kind_(Kind::Number), num_(n) {}
  Json(int64_t n) : kind_(Kind::Number), num_(static_cast<double>(n)) {}
  Json(double n) : kind_(Kind::Number), num_(n) {}
  Json(std::string s) : kind_(Kind::String), str_(std::move(s)) {}
  Json(const char* s) : kind_(Kind::String), str_(s) {}

  static Json array();
  static Json object();

  Kind kind() const { return kind_; }
  bool isNull() const { return kind_ == Kind::Null; }
  bool isBool() const { return kind_ == Kind::Bool; }
  bool isNumber() const { return kind_ == Kind::Number; }
  bool isString() const { return kind_ == Kind::String; }
  bool isArray() const { return kind_ == Kind::Array; }
  bool isObject() const { return kind_ == Kind::Object; }

  bool asBool(bool def = false) const { return isBool() ? bool_ : def; }
  double asNumber(double def = 0) const { return isNumber() ? num_ : def; }
  int64_t asInt(int64_t def = 0) const { return isNumber() ? static_cast<int64_t>(num_) : def; }
  const std::string& asString() const;  // "" si no es cadena

  // Arrays
  size_t size() const { return isArray() ? items_.size() : (isObject() ? keys_.size() : 0); }
  const Json& at(size_t i) const;  // null si fuera de rango
  void push(Json v);

  // Objects (orden de inserción)
  bool has(std::string_view key) const;
  const Json& get(std::string_view key) const;  // null si no existe
  void set(std::string key, Json v);
  const std::vector<std::string>& keys() const { return keys_; }

  // Serializa en una línea, sin espacios.
  std::string dump() const;

  // Parsea `text`. Devuelve false y rellena `error` si hay error de sintaxis o
  // anidamiento excesivo. Rechaza texto sobrante tras el valor.
  static bool parse(std::string_view text, Json& out, std::string& error);

private:
  Kind kind_ = Kind::Null;
  bool bool_ = false;
  double num_ = 0;
  std::string str_;
  std::vector<Json> items_;        // Array
  std::vector<std::string> keys_;  // Object: claves...
  std::vector<Json> vals_;         // ...y valores en paralelo
};

// Escapa una cadena como literal JSON, con comillas.
std::string jsonQuote(std::string_view s);

}  // namespace lsp
