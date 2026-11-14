#include "tools/lsp/json.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace lsp {
namespace {

const Json kNull;
const std::string kEmpty;
constexpr int kMaxDepth = 256;

void appendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out += static_cast<char>(cp);
  } else if (cp < 0x800) {
    out += static_cast<char>(0xC0 | (cp >> 6));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else if (cp < 0x10000) {
    out += static_cast<char>(0xE0 | (cp >> 12));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (cp >> 18));
    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (cp & 0x3F));
  }
}

class Parser {
public:
  Parser(std::string_view t) : s_(t) {}

  bool run(Json& out, std::string& error) {
    skipWs();
    if (!value(out, 0)) {
      error = err_.empty() ? "valor JSON no válido" : err_;
      error += " (posición " + std::to_string(pos_) + ")";
      return false;
    }
    skipWs();
    if (pos_ != s_.size()) {
      error = "texto sobrante tras el valor JSON (posición " + std::to_string(pos_) + ")";
      return false;
    }
    return true;
  }

private:
  std::string_view s_;
  size_t pos_ = 0;
  std::string err_;

  bool fail(const char* msg) {
    if (err_.empty()) err_ = msg;
    return false;
  }

  void skipWs() {
    while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) ++pos_;
  }

  bool literal(std::string_view word) {
    if (s_.substr(pos_, word.size()) != word) return fail("literal no válido");
    pos_ += word.size();
    return true;
  }

  bool hex4(uint32_t& v) {
    if (pos_ + 4 > s_.size()) return fail("escape \\u incompleto");
    v = 0;
    for (int i = 0; i < 4; ++i) {
      char c = s_[pos_++];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= static_cast<uint32_t>(c - '0');
      else if (c >= 'a' && c <= 'f') v |= static_cast<uint32_t>(c - 'a' + 10);
      else if (c >= 'A' && c <= 'F') v |= static_cast<uint32_t>(c - 'A' + 10);
      else return fail("dígito hexadecimal no válido en \\u");
    }
    return true;
  }

  bool string(std::string& out) {
    if (pos_ >= s_.size() || s_[pos_] != '"') return fail("se esperaba una cadena");
    ++pos_;
    while (pos_ < s_.size()) {
      unsigned char c = static_cast<unsigned char>(s_[pos_++]);
      if (c == '"') return true;
      if (c < 0x20) return fail("carácter de control sin escapar en cadena");
      if (c != '\\') {
        out += static_cast<char>(c);
        continue;
      }
      if (pos_ >= s_.size()) break;
      char e = s_[pos_++];
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          uint32_t cp = 0;
          if (!hex4(cp)) return false;
          if (cp >= 0xD800 && cp <= 0xDBFF) {
            uint32_t lo = 0;
            if (pos_ + 2 <= s_.size() && s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
              pos_ += 2;
              if (!hex4(lo)) return false;
              if (lo < 0xDC00 || lo > 0xDFFF) return fail("par sustituto no válido");
              cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
            } else {
              cp = 0xFFFD;  // sustituto suelto: se sustituye
            }
          } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
            cp = 0xFFFD;
          }
          appendUtf8(out, cp);
          break;
        }
        default:
          return fail("escape no válido en cadena");
      }
    }
    return fail("cadena sin cerrar");
  }

  bool number(Json& out) {
    size_t start = pos_;
    if (pos_ < s_.size() && s_[pos_] == '-') ++pos_;
    if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_]))) return fail("número no válido");
    if (s_[pos_] == '0') {
      ++pos_;
    } else {
      while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    }
    if (pos_ < s_.size() && s_[pos_] == '.') {
      ++pos_;
      if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_]))) return fail("número no válido");
      while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    }
    if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
      if (pos_ >= s_.size() || !std::isdigit(static_cast<unsigned char>(s_[pos_]))) return fail("exponente no válido");
      while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    }
    std::string text(s_.substr(start, pos_ - start));
    out = Json(std::strtod(text.c_str(), nullptr));
    return true;
  }

  bool value(Json& out, int depth) {
    if (depth > kMaxDepth) return fail("anidamiento JSON demasiado profundo");
    if (pos_ >= s_.size()) return fail("fin inesperado del texto");
    char c = s_[pos_];
    if (c == '{') {
      ++pos_;
      out = Json::object();
      skipWs();
      if (pos_ < s_.size() && s_[pos_] == '}') {
        ++pos_;
        return true;
      }
      for (;;) {
        skipWs();
        std::string key;
        if (!string(key)) return false;
        skipWs();
        if (pos_ >= s_.size() || s_[pos_] != ':') return fail("se esperaba ':' tras la clave");
        ++pos_;
        skipWs();
        Json v;
        if (!value(v, depth + 1)) return false;
        out.set(std::move(key), std::move(v));
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == ',') {
          ++pos_;
          continue;
        }
        if (pos_ < s_.size() && s_[pos_] == '}') {
          ++pos_;
          return true;
        }
        return fail("se esperaba ',' o '}' en objeto");
      }
    }
    if (c == '[') {
      ++pos_;
      out = Json::array();
      skipWs();
      if (pos_ < s_.size() && s_[pos_] == ']') {
        ++pos_;
        return true;
      }
      for (;;) {
        skipWs();
        Json v;
        if (!value(v, depth + 1)) return false;
        out.push(std::move(v));
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == ',') {
          ++pos_;
          continue;
        }
        if (pos_ < s_.size() && s_[pos_] == ']') {
          ++pos_;
          return true;
        }
        return fail("se esperaba ',' o ']' en array");
      }
    }
    if (c == '"') {
      std::string str;
      if (!string(str)) return false;
      out = Json(std::move(str));
      return true;
    }
    if (c == 't') { if (!literal("true")) return false; out = Json(true); return true; }
    if (c == 'f') { if (!literal("false")) return false; out = Json(false); return true; }
    if (c == 'n') { if (!literal("null")) return false; out = Json(); return true; }
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) return number(out);
    return fail("carácter inesperado");
  }
};

void dumpTo(const Json& v, std::string& out);

void dumpNumber(double d, std::string& out) {
  if (!std::isfinite(d)) {
    out += "null";
    return;
  }
  char buf[64];
  if (std::floor(d) == d && std::fabs(d) < 9007199254740992.0) {
    std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(d));
  } else {
    std::snprintf(buf, sizeof buf, "%.17g", d);
  }
  out += buf;
}

void dumpTo(const Json& v, std::string& out) {
  switch (v.kind()) {
    case Json::Kind::Null: out += "null"; break;
    case Json::Kind::Bool: out += v.asBool() ? "true" : "false"; break;
    case Json::Kind::Number: dumpNumber(v.asNumber(), out); break;
    case Json::Kind::String: out += jsonQuote(v.asString()); break;
    case Json::Kind::Array: {
      out += '[';
      for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += ',';
        dumpTo(v.at(i), out);
      }
      out += ']';
      break;
    }
    case Json::Kind::Object: {
      out += '{';
      const auto& keys = v.keys();
      for (size_t i = 0; i < keys.size(); ++i) {
        if (i) out += ',';
        out += jsonQuote(keys[i]);
        out += ':';
        dumpTo(v.get(keys[i]), out);
      }
      out += '}';
      break;
    }
  }
}

}  // namespace

Json Json::array() {
  Json j;
  j.kind_ = Kind::Array;
  return j;
}

Json Json::object() {
  Json j;
  j.kind_ = Kind::Object;
  return j;
}

const std::string& Json::asString() const { return isString() ? str_ : kEmpty; }

const Json& Json::at(size_t i) const {
  if (!isArray() || i >= items_.size()) return kNull;
  return items_[i];
}

void Json::push(Json v) {
  if (!isArray()) {
    *this = array();
  }
  items_.push_back(std::move(v));
}

bool Json::has(std::string_view key) const {
  if (!isObject()) return false;
  for (const auto& k : keys_) {
    if (k == key) return true;
  }
  return false;
}

const Json& Json::get(std::string_view key) const {
  if (!isObject()) return kNull;
  for (size_t i = 0; i < keys_.size(); ++i) {
    if (keys_[i] == key) return vals_[i];
  }
  return kNull;
}

void Json::set(std::string key, Json v) {
  if (!isObject()) *this = object();
  for (size_t i = 0; i < keys_.size(); ++i) {
    if (keys_[i] == key) {
      vals_[i] = std::move(v);
      return;
    }
  }
  keys_.push_back(std::move(key));
  vals_.push_back(std::move(v));
}

std::string Json::dump() const {
  std::string out;
  dumpTo(*this, out);
  return out;
}

bool Json::parse(std::string_view text, Json& out, std::string& error) {
  Parser p(text);
  return p.run(out, error);
}

std::string jsonQuote(std::string_view s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  out += '"';
  return out;
}

}  // namespace lsp
