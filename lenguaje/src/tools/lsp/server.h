// Servidor de lenguaje (LSP) para lglsp: JSON-RPC 2.0 con cabecera Content-Length sobre
// un flujo de entrada/salida (stdin/stdout en el binario).
#pragma once

#include <iosfwd>
#include <map>
#include <memory>
#include <string>

#include "tools/lsp/analysis.h"
#include "tools/lsp/json.h"

namespace lsp {

class Server {
public:
  explicit Server(std::ostream& out) : out_(out) {}

  // Lee mensajes de `in` hasta EOF o hasta la notificación `exit`. Devuelve el código de
  // salida: 0 si hubo `shutdown` antes de `exit`, 1 en otro caso.
  int run(std::istream& in);

  // Procesa un mensaje JSON-RPC ya leído (sin cabecera). Nunca lanza excepciones.
  void handle(const std::string& body);

  static constexpr size_t kMaxMessageBytes = 64u * 1024 * 1024;

private:
  struct Doc {
    std::string path;
    std::string text;
    int version = 0;
    bool dirty = true;
    std::unique_ptr<Snapshot> snap;
  };

  std::ostream& out_;
  std::map<std::string, Doc> docs_;  // por URI
  bool shutdown_ = false;
  bool exitRequested_ = false;
  int exitCode_ = 1;

  void send(const Json& msg);
  void respond(const Json& id, Json result);
  void respondError(const Json& id, int code, const std::string& message);
  void notify(const std::string& method, Json params);

  void dispatch(const std::string& method, const Json& params, bool hasId, const Json& id);

  void didOpen(const Json& params);
  void didChange(const Json& params);
  void didSave(const Json& params);
  void didClose(const Json& params);
  void publishDiagnostics(const std::string& uri);

  Snapshot* snapshotOf(Doc& d);
  Json definition(const Json& params);
  Json hover(const Json& params);
  Json completion(const Json& params);
};

}  // namespace lsp
