// Prueba unitaria del JSON mínimo del servidor LSP (sin dependencias de test).
#include <cstdio>
#include <string>

#include "tools/lsp/json.h"

namespace {

int failures = 0;
int checks = 0;

#define CHECK(cond)                                                     \
  do {                                                                  \
    ++checks;                                                           \
    if (!(cond)) {                                                      \
      ++failures;                                                       \
      std::fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    }                                                                   \
  } while (0)

bool parses(const std::string& text, lsp::Json& out) {
  std::string err;
  bool ok = lsp::Json::parse(text, out, err);
  if (!ok) std::fprintf(stderr, "  error de parseo: %s\n", err.c_str());
  return ok;
}

bool rejects(const std::string& text) {
  lsp::Json j;
  std::string err;
  return !lsp::Json::parse(text, j, err);
}

}  // namespace

int main() {
  using lsp::Json;

  // Valores escalares y round-trip
  Json j;
  CHECK(parses("null", j) && j.isNull());
  CHECK(parses(" true ", j) && j.asBool() == true);
  CHECK(parses("false", j) && j.asBool(true) == false);
  CHECK(parses("-12.5e2", j) && j.asNumber() == -1250.0);
  CHECK(parses("\"hola\"", j) && j.asString() == "hola");
  CHECK(Json(42).dump() == "42");
  CHECK(Json(-7).dump() == "-7");
  CHECK(Json(0.5).dump() == "0.5");
  CHECK(Json().dump() == "null");

  // Escapes y Unicode
  CHECK(parses("\"a\\nb\\t\\\"c\\\\\"", j) && j.asString() == "a\nb\t\"c\\");
  CHECK(parses("\"\\u00e9\"", j) && j.asString() == "\xc3\xa9");          // é
  CHECK(parses("\"\\ud83d\\ude00\"", j) && j.asString() == "\xf0\x9f\x98\x80");  // emoji (par sustituto)
  CHECK(Json("x\"y\n\x01").dump() == "\"x\\\"y\\n\\u0001\"");

  // Objetos y arrays, orden de claves y acceso
  CHECK(parses("{\"id\": 3, \"method\": \"m\", \"params\": {\"a\": [1, 2, {\"b\": null}]}}", j));
  CHECK(j.isObject() && j.has("id") && !j.has("nada"));
  CHECK(j.get("id").asInt() == 3);
  CHECK(j.get("method").asString() == "m");
  CHECK(j.get("params").get("a").size() == 3);
  CHECK(j.get("params").get("a").at(1).asInt() == 2);
  CHECK(j.get("params").get("a").at(2).get("b").isNull());
  CHECK(j.get("params").get("a").at(99).isNull());
  CHECK(j.keys().size() == 3 && j.keys()[0] == "id" && j.keys()[2] == "params");
  CHECK(j.dump() == "{\"id\":3,\"method\":\"m\",\"params\":{\"a\":[1,2,{\"b\":null}]}}");

  Json o = Json::object();
  o.set("z", 1);
  o.set("a", "x");
  o.set("z", 2);  // reemplaza, no duplica
  CHECK(o.dump() == "{\"z\":2,\"a\":\"x\"}");
  Json arr = Json::array();
  arr.push(1);
  arr.push("dos");
  CHECK(arr.dump() == "[1,\"dos\"]");
  CHECK(Json::array().dump() == "[]" && Json::object().dump() == "{}");

  // Entradas mal formadas: deben rechazarse sin excepciones
  CHECK(rejects(""));
  CHECK(rejects("{"));
  CHECK(rejects("{\"a\" 1}"));
  CHECK(rejects("{\"a\": 1,}"));
  CHECK(rejects("[1, 2"));
  CHECK(rejects("[1 2]"));
  CHECK(rejects("tru"));
  CHECK(rejects("01x"));
  CHECK(rejects("1."));
  CHECK(rejects("\"sin cerrar"));
  CHECK(rejects("\"control \x01 dentro\""));
  CHECK(rejects("\"\\q\""));
  CHECK(rejects("\"\\u12G4\""));
  CHECK(rejects("{} basura"));
  CHECK(rejects("nan"));

  // Anidamiento excesivo: rechazo, no desbordamiento de pila
  std::string deep(100000, '[');
  CHECK(rejects(deep));

  // Texto grande razonable
  std::string big = "[";
  for (int i = 0; i < 20000; ++i) {
    if (i) big += ",";
    big += "{\"k\":" + std::to_string(i) + "}";
  }
  big += "]";
  CHECK(parses(big, j) && j.size() == 20000 && j.at(19999).get("k").asInt() == 19999);

  std::printf("json_test: %d comprobaciones, %d fallos\n", checks, failures);
  return failures == 0 ? 0 : 1;
}
