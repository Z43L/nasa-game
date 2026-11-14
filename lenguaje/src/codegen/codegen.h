// Generación de código LLVM (propietario: agente de codegen).
// Entrada: AST ya anotado por sema (tipos, CallKind, targets, mangled names).
//
// Contrato de memoria (ver comentarios en codegen.cpp):
//   - Toda expresión produce un valor POSEÍDO (+1) si su tipo es heap.
//   - Las funciones reciben parámetros prestados y devuelven valores poseídos.
#pragma once

#include <string>
#include <vector>

#include "ast/ast.h"
#include "common/diagnostics.h"
#include "sema/types.h"

namespace lg {

// optLevel: 0..3 (-O0..-O3). Genera el módulo para el host, optimiza y emite un .o nativo.
// debug: añade información DWARF (lgc -g); con false el módulo es el de siempre.
// wasm: genera para wasm32-wasip1 (lgc --target wasm32) en lugar del host. Falla con un
// diagnóstico si el programa usa hilos, canales o tcp (no disponibles en wasm).
// exports (solo wasm): funciones de nivel superior exportadas con su nombre del .lg (lgc --export).
// Cada una genera un envoltorio "lgx_<nombre>" con wasm-export-name. Ver codegen.cpp (genExport).
bool emitObjectFile(const Program& prog, const TypeTable& types, const std::string& objPath,
                    int optLevel, Diagnostics& diags, bool debug = false, bool wasm = false,
                    const std::vector<std::string>& exports = {});

// Devuelve el IR textual del módulo (sin optimizar). debug y wasm: ver emitObjectFile.
bool emitLLVMIR(const Program& prog, const TypeTable& types, std::string& irOut, Diagnostics& diags,
                bool debug = false, bool wasm = false, const std::vector<std::string>& exports = {});

}  // namespace lg
