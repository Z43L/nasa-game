#include "tools/lsp/server.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <set>
#include <string>

namespace lsp {
namespace {

// ---------- posiciones: el compilador cuenta columnas en bytes (1-based); LSP en unidades
// UTF-16 (0-based). Aquí se convierten entre ambas.

std::string_view lineOf(const std::string& text, int line1) {
  if (line1 < 1) return {};
  size_t start = 0;
  for (int l = 1; l < line1; ++l) {
    size_t nl = text.find('\n', start);
    if (nl == std::string::npos) return {};
    start = nl + 1;
  }
  size_t end = text.find('\n', start);
  if (end == std::string::npos) end = text.size();
  std::string_view v(text.data() + start, end - start);
  if (!v.empty() && v.back() == '\r') v.remove_suffix(1);
  return v;
}

bool isCont(unsigned char c) { return (c & 0xC0) == 0x80; }

// Columna 1-based en bytes -> carácter 0-based en UTF-16.
int byteColToUtf16(std::string_view line, int col1) {
  int units = 0;
  size_t limit = col1 > 1 ? static_cast<size_t>(col1 - 1) : 0;
  for (size_t i = 0; i < line.size() && i < limit; ++i) {
    unsigned char c = static_cast<unsigned char>(line[i]);
    if (isCont(c)) continue;
    units += (c >= 0xF0) ? 2 : 1;
  }
  return units;
}

// Carácter 0-based UTF-16 -> columna 1-based en bytes.
int utf16ToByteCol(std::string_view line, int u16) {
  size_t i = 0;
  int units = 0;
  while (i < line.size() && units < u16) {
    unsigned char c = static_cast<unsigned char>(line[i]);
    if (isCont(c)) {
      ++i;
      continue;
    }
    units += (c >= 0xF0) ? 2 : 1;
    ++i;
  }
  while (i < line.size() && isCont(static_cast<unsigned char>(line[i]))) ++i;
  return static_cast<int>(i) + 1;
}

// Longitud en bytes de un identificador que empieza en col1 (al menos 1 byte).
int identLenAt(std::string_view line, int col1) {
  size_t i = col1 > 1 ? static_cast<size_t>(col1 - 1) : 0;
  size_t start = i;
  while (i < line.size()) {
    unsigned char c = static_cast<unsigned char>(line[i]);
    if (!(std::isalnum(c) || c == '_' || c >= 0x80)) break;
    ++i;
  }
  return i > start ? static_cast<int>(i - start) : 1;
}

Json positionJson(std::string_view line, int line1, int col1) {
  Json p = Json::object();
  p.set("line", line1 - 1);
  p.set("character", byteColToUtf16(line, col1));
  return p;
}

Json rangeJson(const std::string& text, int line1, int col1, int len) {
  std::string_view line = lineOf(text, line1);
  Json r = Json::object();
  r.set("start", positionJson(line, line1, col1));
  Json end = Json::object();
  end.set("line", line1 - 1);
  end.set("character", byteColToUtf16(line, col1 + len));
  r.set("end", end);
  return r;
}

std::string uriFromPath(const std::string& path) {
  static const char* hex = "0123456789ABCDEF";
  std::string out = "file://";
  for (unsigned char c : path) {
    if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') {
      out += static_cast<char>(c);
    } else {
      out += '%';
      out += hex[c >> 4];
      out += hex[c & 15];
    }
  }
  return out;
}

std::string pathFromUri(const std::string& uri) {
  if (uri.rfind("file://", 0) != 0) return uri;
  std::string out;
  std::string raw = uri.substr(7);
  for (size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] == '%' && i + 2 < raw.size() && std::isxdigit(static_cast<unsigned char>(raw[i + 1])) &&
        std::isxdigit(static_cast<unsigned char>(raw[i + 2]))) {
      out += static_cast<char>(std::stoi(raw.substr(i + 1, 2), nullptr, 16));
      i += 2;
    } else {
      out += raw[i];
    }
  }
  return out;
}

// Token de identificador que contiene (line, col) o empieza ahí, o nullptr.
const lg::Token* identTokenAt(const std::vector<lg::Token>& toks, int line, int col, size_t* index = nullptr) {
  // último token con posición <= (line, col)
  auto it = std::upper_bound(toks.begin(), toks.end(), std::make_pair(line, col),
                             [](const std::pair<int, int>& p, const lg::Token& t) {
                               return p < std::make_pair(t.line, t.col);
                             });
  if (it == toks.begin()) return nullptr;
  --it;
  if (it->kind != lg::TokenKind::Ident) return nullptr;
  if (line != it->line || col >= it->col + static_cast<int>(it->text.size())) return nullptr;
  if (index) *index = static_cast<size_t>(it - toks.begin());
  return &*it;
}

Json makeLocation(const std::string& uri, const std::string& text, int line, int col, int len) {
  Json loc = Json::object();
  loc.set("uri", uri);
  loc.set("range", rangeJson(text, line, col, len));
  return loc;
}

int lspSeverity(lg::Diagnostic::Severity s) {
  switch (s) {
    case lg::Diagnostic::Severity::Error: return 1;
    case lg::Diagnostic::Severity::Warning: return 2;
    case lg::Diagnostic::Severity::Note: return 3;
  }
  return 1;
}

}  // namespace

// ---------- transporte ----------

void Server::send(const Json& msg) {
  const std::string body = msg.dump();
  out_ << "Content-Length: " << body.size() << "\r\n\r\n" << body;
  out_.flush();
}

void Server::respond(const Json& id, Json result) {
  Json m = Json::object();
  m.set("jsonrpc", "2.0");
  m.set("id", id);
  m.set("result", std::move(result));
  send(m);
}

void Server::respondError(const Json& id, int code, const std::string& message) {
  Json err = Json::object();
  err.set("code", code);
  err.set("message", message);
  Json m = Json::object();
  m.set("jsonrpc", "2.0");
  m.set("id", id);
  m.set("error", std::move(err));
  send(m);
}

void Server::notify(const std::string& method, Json params) {
  Json m = Json::object();
  m.set("jsonrpc", "2.0");
  m.set("method", method);
  m.set("params", std::move(params));
  send(m);
}

int Server::run(std::istream& in) {
  for (;;) {
    long long length = -1;
    std::string line;
    bool eof = false;
    for (;;) {  // cabeceras
      if (!std::getline(in, line)) {
        eof = true;
        break;
      }
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.empty()) break;
      const size_t colon = line.find(':');
      if (colon == std::string::npos) continue;
      std::string name = line.substr(0, colon);
      for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      if (name == "content-length") {
        std::string v = line.substr(colon + 1);
        const size_t first = v.find_first_not_of(" \t");
        if (first == std::string::npos || !std::isdigit(static_cast<unsigned char>(v[first]))) {
          length = -2;  // inválida
        } else {
          length = std::atoll(v.c_str() + first);
        }
      }
    }
    if (eof) break;
    if (length < 0) {
      std::cerr << "lglsp: cabecera Content-Length ausente o no válida; mensaje ignorado\n";
      continue;
    }
    if (static_cast<unsigned long long>(length) > kMaxMessageBytes) {
      std::cerr << "lglsp: mensaje demasiado grande (" << length << " bytes); descartado\n";
      in.ignore(length);
      continue;
    }
    std::string body(static_cast<size_t>(length), '\0');
    in.read(&body[0], length);
    if (in.gcount() != length) break;  // flujo truncado
    handle(body);
    if (exitRequested_) break;
  }
  return exitCode_;
}

void Server::handle(const std::string& body) {
  Json msg;
  std::string err;
  if (!Json::parse(body, msg, err)) {
    respondError(Json(), -32700, "Parse error: " + err);
    return;
  }
  if (!msg.isObject()) {
    respondError(Json(), -32600, "Invalid Request: se esperaba un objeto JSON");
    return;
  }
  const bool hasId = msg.has("id");
  const Json id = msg.get("id");
  const Json& method = msg.get("method");
  if (!method.isString()) {
    // Respuesta del cliente a una petición nuestra (no enviamos ninguna): se ignora.
    if (msg.has("result") || msg.has("error")) return;
    respondError(hasId ? id : Json(), -32600, "Invalid Request: falta 'method'");
    return;
  }
  try {
    dispatch(method.asString(), msg.get("params"), hasId, id);
  } catch (const std::exception& e) {
    std::cerr << "lglsp: error interno en " << method.asString() << ": " << e.what() << "\n";
    if (hasId) respondError(id, -32603, std::string("Internal error: ") + e.what());
  } catch (...) {
    if (hasId) respondError(id, -32603, "Internal error");
  }
}

void Server::dispatch(const std::string& method, const Json& params, bool hasId, const Json& id) {
  if (shutdown_ && method != "exit") {
    if (hasId) respondError(id, -32600, "Invalid Request: el servidor está en apagado");
    return;
  }

  if (method == "initialize") {
    if (!hasId) return;
    Json caps = Json::object();
    Json sync = Json::object();
    sync.set("openClose", true);
    sync.set("change", 1);  // sincronización completa
    Json save = Json::object();
    save.set("includeText", false);
    sync.set("save", save);
    caps.set("textDocumentSync", sync);
    caps.set("definitionProvider", true);
    caps.set("hoverProvider", true);
    Json comp = Json::object();
    Json trig = Json::array();
    trig.push(".");
    comp.set("triggerCharacters", trig);
    caps.set("completionProvider", comp);
    Json info = Json::object();
    info.set("name", "lglsp");
    info.set("version", "0.1.0");
    Json result = Json::object();
    result.set("capabilities", caps);
    result.set("serverInfo", info);
    respond(id, result);
  } else if (method == "initialized") {
    // sin acción
  } else if (method == "shutdown") {
    shutdown_ = true;
    if (hasId) respond(id, Json());
  } else if (method == "exit") {
    exitCode_ = shutdown_ ? 0 : 1;
    exitRequested_ = true;
  } else if (method == "textDocument/didOpen") {
    didOpen(params);
  } else if (method == "textDocument/didChange") {
    didChange(params);
  } else if (method == "textDocument/didSave") {
    didSave(params);
  } else if (method == "textDocument/didClose") {
    didClose(params);
  } else if (method == "textDocument/definition") {
    if (hasId) respond(id, definition(params));
  } else if (method == "textDocument/hover") {
    if (hasId) respond(id, hover(params));
  } else if (method == "textDocument/completion") {
    if (hasId) respond(id, completion(params));
  } else if (method.rfind("$/", 0) == 0) {
    // notificaciones internas del protocolo: se ignoran
    if (hasId) respondError(id, -32601, "método no soportado: " + method);
  } else if (hasId) {
    respondError(id, -32601, "método no soportado: " + method);
  }
}

// ---------- documentos ----------

void Server::didOpen(const Json& params) {
  const Json& td = params.get("textDocument");
  const std::string uri = td.get("uri").asString();
  if (uri.empty()) return;
  Doc& d = docs_[uri];
  d.path = pathFromUri(uri);
  d.text = td.get("text").asString();
  d.version = static_cast<int>(td.get("version").asInt());
  d.dirty = true;
  publishDiagnostics(uri);
}

void Server::didChange(const Json& params) {
  const Json& td = params.get("textDocument");
  const std::string uri = td.get("uri").asString();
  auto it = docs_.find(uri);
  if (it == docs_.end()) return;
  // Sincronización completa: el último cambio contiene el texto entero.
  const Json& changes = params.get("contentChanges");
  for (size_t i = 0; i < changes.size(); ++i) {
    if (changes.at(i).get("text").isString()) it->second.text = changes.at(i).get("text").asString();
  }
  it->second.version = static_cast<int>(td.get("version").asInt(it->second.version));
  it->second.dirty = true;  // el análisis se rehace en el siguiente guardado o consulta
}

void Server::didSave(const Json& params) {
  const std::string uri = params.get("textDocument").get("uri").asString();
  auto it = docs_.find(uri);
  if (it == docs_.end()) return;
  if (params.get("text").isString()) it->second.text = params.get("text").asString();
  it->second.dirty = true;
  publishDiagnostics(uri);
}

void Server::didClose(const Json& params) {
  const std::string uri = params.get("textDocument").get("uri").asString();
  docs_.erase(uri);
  Json p = Json::object();
  p.set("uri", uri);
  p.set("diagnostics", Json::array());
  notify("textDocument/publishDiagnostics", p);
}

void Server::publishDiagnostics(const std::string& uri) {
  auto it = docs_.find(uri);
  if (it == docs_.end()) return;
  Doc& d = it->second;
  Snapshot* snap = snapshotOf(d);

  Json list = Json::array();
  if (snap) {
    for (const auto& dg : snap->diags.all()) {
      if (dg.loc.file != d.path) continue;  // errores de imports: se publican en su propio archivo (no soportado)
      std::string msg = dg.message;
      if (!dg.hint.empty()) msg += "\nsugerencia: " + dg.hint;
      Json item = Json::object();
      const int line = dg.loc.line < 1 ? 1 : dg.loc.line;
      const int col = dg.loc.col < 1 ? 1 : dg.loc.col;
      std::string_view lv = lineOf(d.text, line);
      const int len = identLenAt(lv, col);
      item.set("range", rangeJson(d.text, line, col, len));
      item.set("severity", lspSeverity(dg.severity));
      item.set("source", "lglsp");
      item.set("message", msg);
      list.push(std::move(item));
    }
  }
  Json p = Json::object();
  p.set("uri", uri);
  p.set("diagnostics", list);
  notify("textDocument/publishDiagnostics", p);
}

Snapshot* Server::snapshotOf(Doc& d) {
  if (d.dirty || !d.snap) {
    try {
      d.snap = buildSnapshot(d.path, d.text);
    } catch (const std::exception& e) {
      std::cerr << "lglsp: fallo al analizar " << d.path << ": " << e.what() << "\n";
      d.snap.reset();
    } catch (...) {
      d.snap.reset();
    }
    d.dirty = false;
  }
  return d.snap.get();
}

// ---------- consultas ----------

Json Server::definition(const Json& params) {
  const std::string uri = params.get("textDocument").get("uri").asString();
  auto it = docs_.find(uri);
  if (it == docs_.end()) return Json();
  Doc& d = it->second;
  Snapshot* snap = snapshotOf(d);
  if (!snap || !snap->parsed) return Json();

  const int line = static_cast<int>(params.get("position").get("line").asInt()) + 1;
  const int col = utf16ToByteCol(lineOf(d.text, line), static_cast<int>(params.get("position").get("character").asInt()));
  auto ft = snap->files.find(d.path);
  if (ft == snap->files.end()) return Json();
  size_t idx = 0;
  const lg::Token* tok = identTokenAt(ft->second.tokens, line, col, &idx);
  if (!tok) return Json();

  const bool afterDot = idx > 0 && ft->second.tokens[idx - 1].kind == lg::TokenKind::Dot;
  const Ref* r = afterDot ? memberRefAt(*snap, d.path, tok->text, tok->line, tok->col)
                          : identRefAt(*snap, d.path, tok->line, tok->col);
  if (!r || r->def < 0) return Json();
  const Def& df = snap->defs[static_cast<size_t>(r->def)];
  if (df.kind == Def::Kind::Builtin || df.file.empty()) return Json();

  auto target = snap->files.find(df.file);
  if (target == snap->files.end()) return Json();
  Json arr = Json::array();
  arr.push(makeLocation(uriFromPath(df.file), target->second.text, df.line, df.col,
                        static_cast<int>(df.name.size())));
  return arr;
}

Json Server::hover(const Json& params) {
  const std::string uri = params.get("textDocument").get("uri").asString();
  auto it = docs_.find(uri);
  if (it == docs_.end()) return Json();
  Doc& d = it->second;
  Snapshot* snap = snapshotOf(d);
  if (!snap || !snap->parsed) return Json();

  const int line = static_cast<int>(params.get("position").get("line").asInt()) + 1;
  const std::string_view lv = lineOf(d.text, line);
  const int col = utf16ToByteCol(lv, static_cast<int>(params.get("position").get("character").asInt()));
  auto ft = snap->files.find(d.path);
  if (ft == snap->files.end()) return Json();
  size_t idx = 0;
  const lg::Token* tok = identTokenAt(ft->second.tokens, line, col, &idx);
  if (!tok) return Json();

  const bool afterDot = idx > 0 && ft->second.tokens[idx - 1].kind == lg::TokenKind::Dot;
  const Ref* r = afterDot ? memberRefAt(*snap, d.path, tok->text, tok->line, tok->col)
                          : identRefAt(*snap, d.path, tok->line, tok->col);
  std::string detail;
  if (r && r->def >= 0) {
    detail = snap->defs[static_cast<size_t>(r->def)].detail;
  } else if (r && !r->detail.empty()) {
    detail = r->detail;
  }
  if (detail.empty()) return Json();

  Json contents = Json::object();
  contents.set("kind", "markdown");
  contents.set("value", "```lg\n" + detail + "\n```");
  Json res = Json::object();
  res.set("contents", contents);
  res.set("range", rangeJson(d.text, tok->line, tok->col, static_cast<int>(tok->text.size())));
  return res;
}

Json Server::completion(const Json& params) {
  const std::string uri = params.get("textDocument").get("uri").asString();
  Json list = Json::object();
  list.set("isIncomplete", false);
  Json items = Json::array();
  list.set("items", items);

  auto it = docs_.find(uri);
  if (it == docs_.end()) return list;
  Doc& d = it->second;
  Snapshot* snap = snapshotOf(d);
  if (!snap || !snap->parsed) return list;
  auto ft = snap->files.find(d.path);
  if (ft == snap->files.end()) return list;
  const auto& toks = ft->second.tokens;

  const int line = static_cast<int>(params.get("position").get("line").asInt()) + 1;
  const std::string_view lv = lineOf(d.text, line);
  const int col = utf16ToByteCol(lv, static_cast<int>(params.get("position").get("character").asInt()));

  // Último token que termina antes del cursor. Si es el prefijo de un identificador, se salta.
  auto end = std::lower_bound(toks.begin(), toks.end(), std::make_pair(line, col),
                              [](const lg::Token& t, const std::pair<int, int>& p) {
                                return std::make_pair(t.line, t.col) < p;
                              });
  size_t before = static_cast<size_t>(end - toks.begin());
  if (before > 0 && toks[before - 1].kind == lg::TokenKind::Ident && toks[before - 1].line == line &&
      toks[before - 1].col + static_cast<int>(toks[before - 1].text.size()) >= col) {
    --before;  // prefijo parcial en curso
  }

  std::set<std::string> seen;
  auto add = [&](const std::string& label, int kind, const std::string& detail) {
    if (!seen.insert(label).second) return;
    Json item = Json::object();
    item.set("label", label);
    item.set("kind", kind);
    if (!detail.empty()) item.set("detail", detail);
    items.push(std::move(item));
  };

  // Acceso con punto: campos y métodos del tipo struct del objeto.
  if (before > 0 && toks[before - 1].kind == lg::TokenKind::Dot) {
    if (before < 2 || toks[before - 2].kind != lg::TokenKind::Ident) return list;
    const lg::Token& recv = toks[before - 2];
    const Ref* r = identRefAt(*snap, d.path, recv.line, recv.col);
    if (!r || r->def < 0) return list;
    const std::string owner = structNameOf(snap->defs[static_cast<size_t>(r->def)].type);
    auto m = snap->membersOf.find(owner);
    if (m == snap->membersOf.end()) return list;
    for (int di : m->second) {
      const Def& df = snap->defs[static_cast<size_t>(di)];
      if (df.kind == Def::Kind::Field) add(df.name, 5, df.detail);
      else if (df.kind == Def::Kind::Method) add(df.name, 2, df.detail);
    }
    list.set("items", items);
    return list;
  }

  // Ámbito general: palabras reservadas, tipos, builtins y nombres declarados en el análisis.
  for (const auto& k : keywordNames()) add(k, 14, "");
  for (const auto& t : typeNames()) add(t, 7, "tipo");
  for (const auto& b : builtinNames()) add(b, 3, "builtin");
  for (const auto& df : snap->defs) {
    switch (df.kind) {
      case Def::Kind::Function: add(df.name, 3, df.detail); break;
      case Def::Kind::Struct: add(df.name, 22, df.detail); break;
      case Def::Kind::Variable:
      case Def::Kind::Parameter: add(df.name, 6, df.detail); break;
      default: break;  // campos, métodos y builtins: fuera del ámbito general
    }
  }
  list.set("items", items);
  return list;
}

}  // namespace lsp
