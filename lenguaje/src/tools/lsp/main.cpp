// lglsp: servidor de lenguaje (LSP) del lenguaje. Habla JSON-RPC por stdin/stdout con
// cabeceras Content-Length. Ver tools/lsp/server.h.
#include <iostream>

#include "tools/lsp/server.h"

int main() {
  std::ios::sync_with_stdio(false);
  lsp::Server server(std::cout);
  return server.run(std::cin);
}
