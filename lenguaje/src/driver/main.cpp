// lgc: compilador del lenguaje.
//   lgc archivo.lg [-o salida] [-I ruta]... [-O0|-O1|-O2|-O3] [-g] [--emit-llvm] [--keep-obj]
//       [--target wasm32] [--export NOMBRE]...
//   --target wasm32: genera WebAssembly (wasm32-wasip1) y enlaza con wasm-ld y wasi-libc.
//   --export NOMBRE: (repetible, solo con wasm32) exporta la función de nivel superior NOMBRE del .lg
//       con ese mismo nombre en el .wasm (instance.exports.NOMBRE). Sin esto no hay exports de usuario.
//   -I ruta: añade una carpeta de búsqueda de imports (antes de la biblioteca estándar).
//   -g: información de depuración DWARF (líneas del .lg y variables locales para lldb/gdb).
//
// Pasos: carga + imports (loader) -> sema -> codegen (.o temporal) -> enlace con cc y liblg_runtime.a.
// --emit-llvm: escribe el IR textual en <salida>.ll (o <stem>.ll) y no enlaza.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "ast/ast.h"
#include "codegen/codegen.h"
#include "common/diagnostics.h"
#include "driver/loader.h"
#include "sema/sema.h"  // bool analyze(Program&, TypeTable&, Diagnostics&) (propietario: sema)
#include "sema/types.h"

#ifndef LG_RUNTIME_LIB
#error "LG_RUNTIME_LIB debe definirlo CMake"
#endif

// Herramientas para --target wasm32 (CMake las define; vacías si no se encontraron).
#ifndef LG_WASM_CC
#define LG_WASM_CC ""
#endif
#ifndef LG_WASM_SYSROOT
#define LG_WASM_SYSROOT ""
#endif
#ifndef LG_WASM_RUNTIME_LIB
#define LG_WASM_RUNTIME_LIB ""
#endif

namespace fs = std::filesystem;

namespace {

// Nombre válido para --export: identificador ASCII que no empieza por '_' ni dígito, y no usa los
// prefijos que reserva el runtime wasm (lg_*) ni los exports de wasi/linker (memory, _start...).
// Devuelve "" si vale, o el motivo.
std::string exportNameProblem(const std::string& e) {
  if (e.empty()) return "nombre vacío";
  if (!((e[0] >= 'a' && e[0] <= 'z') || (e[0] >= 'A' && e[0] <= 'Z')))
    return "debe empezar por una letra ASCII";
  for (char c : e) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    if (!ok) return "solo se admiten letras ASCII, dígitos y '_'";
  }
  if (e.compare(0, 3, "lg_") == 0) return "el prefijo 'lg_' está reservado para el runtime";
  if (e == "memory") return "nombre reservado por el enlazador de wasm";
  return "";
}

void usage() {
  std::cerr << "uso: lgc archivo.lg [-o salida] [-I ruta]... [-O0|-O1|-O2|-O3] [-g] [--emit-llvm] [--keep-obj]\n"
               "           [--target wasm32] [--export NOMBRE]...\n";
}

// Carpeta de la biblioteca estándar junto al binario: <bin>/../share/lenguaje/stdlib.
// Busca el ejecutable por argv[0] o, si no tiene '/', en PATH.
std::string stdlibNextToBinary(const char* argv0) {
  std::string a0 = argv0 ? argv0 : "";
  fs::path exe;
  std::error_code ec;
  if (a0.find('/') != std::string::npos) {
    exe = fs::canonical(a0, ec);
  } else if (const char* path = std::getenv("PATH")) {
    std::string p = path;
    size_t start = 0;
    while (start <= p.size()) {
      size_t end = p.find(':', start);
      if (end == std::string::npos) end = p.size();
      fs::path cand = fs::path(p.substr(start, end - start)) / a0;
      if (!cand.empty() && fs::exists(cand, ec) && !ec) {
        exe = fs::canonical(cand, ec);
        break;
      }
      start = end + 1;
    }
  }
  if (exe.empty() || ec) return "";
  return (exe.parent_path() / ".." / "share" / "lenguaje" / "stdlib").string();
}

// Comillas simples para pasar una ruta al shell sin interpretación.
std::string shellQuote(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "'\\''";
    else out += c;
  }
  return out + "'";
}

}  // namespace

int main(int argc, char** argv) {
  std::string input, output;
  int optLevel = 0;
  bool emitLLVM = false, keepObj = false, debugInfo = false;
  bool wasm = false;
  std::vector<std::string> exportNames;
  lg::LoadOptions loadOpts;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-o" && i + 1 < argc) {
      output = argv[++i];
    } else if (a == "--target" || a.compare(0, 9, "--target=") == 0) {
      std::string t = a == "--target" ? (i + 1 < argc ? argv[++i] : "") : a.substr(9);
      if (t != "wasm32") {
        std::cerr << "lgc: objetivo desconocido '" << t << "' (valores: wasm32)\n";
        return 2;
      }
      wasm = true;
    } else if (a == "--export" && i + 1 < argc) {
      exportNames.push_back(argv[++i]);
    } else if (a.compare(0, 9, "--export=") == 0) {
      exportNames.push_back(a.substr(9));
    } else if (a == "-I" && i + 1 < argc) {
      loadOpts.searchPaths.push_back(argv[++i]);
    } else if (a.size() > 2 && a.compare(0, 2, "-I") == 0) {
      loadOpts.searchPaths.push_back(a.substr(2));
    } else if (a == "-O0" || a == "-O1" || a == "-O2" || a == "-O3") {
      optLevel = a[2] - '0';
    } else if (a == "-g") {
      debugInfo = true;
    } else if (a == "--emit-llvm") {
      emitLLVM = true;
    } else if (a == "--keep-obj") {
      keepObj = true;
    } else if (a == "-h" || a == "--help") {
      usage();
      return 0;
    } else if (!a.empty() && a[0] == '-') {
      std::cerr << "lgc: opción desconocida: " << a << "\n";
      usage();
      return 2;
    } else {
      input = a;
    }
  }
  if (input.empty()) {
    usage();
    return 2;
  }
  if (!exportNames.empty() && !wasm) {
    std::cerr << "lgc: --export solo se admite con --target wasm32\n";
    return 2;
  }
  for (const auto& e : exportNames) {
    std::string problem = exportNameProblem(e);
    if (!problem.empty()) {
      std::cerr << "lgc: error: --export '" << e << "': nombre de export no válido (" << problem << ")\n";
      return 1;
    }
  }

  fs::path stem = fs::path(input).stem();
  if (output.empty()) output = stem.string();

  lg::Diagnostics diags;
  lg::Program prog;

  // Biblioteca estándar, al final de la búsqueda (después de -I y de las dependencias de lgpm).
  std::error_code ec;
  const std::string stdlibBuild = LG_STDLIB_DIR;
  if (fs::is_directory(stdlibBuild, ec)) loadOpts.searchPaths.push_back(stdlibBuild);
  std::string stdlibBin = stdlibNextToBinary(argv[0]);
  if (!stdlibBin.empty() && fs::is_directory(stdlibBin, ec)) loadOpts.searchPaths.push_back(stdlibBin);

  // (a) carga y resolución de imports
  if (!lg::loadProgram(input, prog, diags, loadOpts)) {
    diags.print(std::cerr);
    return 1;
  }

  // (b) análisis semántico y anotación de tipos
  lg::TypeTable types;
  if (!lg::analyze(prog, types, diags) || diags.hasErrors()) {
    diags.print(std::cerr);
    return 1;
  }

  if (emitLLVM) {
    std::string ir;
    if (!lg::emitLLVMIR(prog, types, ir, diags, debugInfo, wasm, exportNames)) {
      diags.print(std::cerr);
      return 1;
    }
    std::string irPath = output + ".ll";
    std::ofstream os(irPath);
    if (!os) {
      std::cerr << "lgc: no se puede escribir " << irPath << "\n";
      return 1;
    }
    os << ir;
    return 0;
  }

  // (c) objeto temporal junto a la salida
  std::string objPath = output + ".o";
  if (!lg::emitObjectFile(prog, types, objPath, optLevel, diags, debugInfo, wasm, exportNames)) {
    diags.print(std::cerr);
    return 1;
  }
  // Si hubo solo warnings, se muestran igualmente.
  if (!diags.all().empty()) diags.print(std::cerr);

  if (wasm) {
    // (d-wasm) enlace con clang (driver de wasm-ld), wasi-libc y el runtime wasm32.
    if (std::string(LG_WASM_CC).empty() || std::string(LG_WASM_SYSROOT).empty() ||
        std::string(LG_WASM_RUNTIME_LIB).empty()) {
      std::cerr << "lgc: --target wasm32 no está disponible en esta compilación "
                   "(falta clang de LLVM, wasi-libc o el runtime wasm32; ver CMakeLists.txt)\n";
      std::remove(objPath.c_str());
      return 1;
    }
    std::string cmd = shellQuote(LG_WASM_CC) + " --target=wasm32-wasip1 --sysroot=" +
                      shellQuote(LG_WASM_SYSROOT) + " -o " + shellQuote(output) + " " +
                      shellQuote(objPath) + " " + shellQuote(LG_WASM_RUNTIME_LIB) +
                      " -nodefaultlibs -lc";  // sin libclang_rt (no instalada): wasi-libc basta
    int rc = std::system(cmd.c_str());
    if (!keepObj) std::remove(objPath.c_str());
    if (rc != 0) {
      std::cerr << "lgc: error al enlazar para wasm32 (" << cmd << ")\n";
      return 1;
    }
    return 0;
  }

  // (d) enlace con cc + runtime + libm.
  // En Linux, el runtime usa pthread_getattr_np: se añade -pthread (necesario en glibc < 2.34).
  // El objeto lo genera codegen con relocaciones PIC y el runtime se compila PIC, así que no
  // hace falta -no-pie: el enlace funciona con PIE (valor por defecto) y sin PIE.
#if defined(__linux__)
  const std::string sysLibs = " -pthread -lm";
#else
  const std::string sysLibs = " -lm";
#endif
  std::string cmd = "cc -o " + shellQuote(output) + " " + shellQuote(objPath) + " " +
                    shellQuote(LG_RUNTIME_LIB) + sysLibs;
  int rc = std::system(cmd.c_str());
#if defined(__APPLE__)
  // En macOS el DWARF queda en el .o; dsymutil lo copia a output.dSYM para que lldb lo encuentre.
  if (rc == 0 && debugInfo) {
    std::string ds = "dsymutil " + shellQuote(output) + " -o " + shellQuote(output + ".dSYM") +
                     " >/dev/null 2>&1";
    if (std::system(ds.c_str()) != 0)
      std::cerr << "lgc: aviso: no se pudo generar " << output << ".dSYM con dsymutil\n";
  }
#endif
  if (!keepObj) std::remove(objPath.c_str());
  if (rc != 0) {
    std::cerr << "lgc: error al enlazar (" << cmd << ")\n";
    return 1;
  }
  return 0;
}
