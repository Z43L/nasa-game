// lgfmt: formateador canónico de código .lg.
//   lgfmt archivo.lg [más.lg ...]       imprime el formato en stdout
//   lgfmt -i archivo.lg [...]           reescribe los archivos in situ
//   lgfmt --check archivo.lg [...]      sale con 1 si algún archivo no está formateado
//   lgfmt --dump-ast archivo.lg         (depuración) imprime el volcado canónico del AST
// Errores de parseo: diagnóstico estándar en stderr, salida 1, ningún archivo se modifica.
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "common/diagnostics.h"
#include "format.h"
#include "parser/parser.h"

namespace {

void usage() {
  std::cerr << "uso: lgfmt [-i | --check] archivo.lg [más.lg ...]\n"
               "     lgfmt --dump-ast archivo.lg\n";
}

bool readFile(const std::string& path, std::string& text) {
  std::ifstream is(path, std::ios::binary);
  if (!is) return false;
  std::ostringstream ss;
  ss << is.rdbuf();
  text = ss.str();
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  bool inPlace = false, check = false, dumpAst = false;
  std::vector<std::string> files;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "-i") {
      inPlace = true;
    } else if (a == "--check") {
      check = true;
    } else if (a == "--dump-ast") {
      dumpAst = true;
    } else if (a == "-h" || a == "--help") {
      usage();
      return 0;
    } else if (!a.empty() && a[0] == '-') {
      std::cerr << "lgfmt: opción desconocida: " << a << "\n";
      usage();
      return 2;
    } else {
      files.push_back(a);
    }
  }
  if (files.empty() || (inPlace && check)) {
    usage();
    return 2;
  }

  int rc = 0;
  for (const std::string& file : files) {
    std::string src;
    if (!readFile(file, src)) {
      std::cerr << "lgfmt: no se puede leer " << file << "\n";
      rc = 1;
      continue;
    }
    lg::Diagnostics diags;

    if (dumpAst) {
      auto prog = lg::parseProgram(file, src, diags);
      if (diags.hasErrors()) {
        diags.print(std::cerr);
        rc = 1;
        continue;
      }
      std::cout << lgfmt::dumpProgram(*prog);
      continue;
    }

    std::string formatted, internal;
    if (!lgfmt::formatSource(file, src, formatted, diags, internal)) {
      if (internal.empty()) {
        diags.print(std::cerr);
      } else {
        std::cerr << "lgfmt: " << file << ": error interno: " << internal << "\n";
      }
      rc = 1;
      continue;
    }

    const bool changed = formatted != src;
    if (check) {
      if (changed) {
        std::cerr << "lgfmt: " << file << " no está formateado\n";
        rc = 1;
      }
      continue;
    }
    if (inPlace) {
      if (changed) {
        std::ofstream os(file, std::ios::binary | std::ios::trunc);
        os << formatted;
        os.close();
        if (!os) {
          std::cerr << "lgfmt: no se puede escribir " << file << "\n";
          rc = 1;
        }
      }
      continue;
    }
    std::cout << formatted;
  }
  return rc;
}
