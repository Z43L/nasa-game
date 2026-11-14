// Información de depuración DWARF (propietario: agente de depurador).
// Se activa con `lgc -g`. Sin -g, codegen no crea ningún objeto de esta clase y el IR no cambia.
//
// Qué emite:
//  - DICompileUnit y DIFile por archivo fuente (el principal y cada import).
//  - DISubprogram por función de usuario, método, función anidada/clausura y `main`.
//  - DILocation en cada sentencia y expresión (codegen llama a DbgLocGuard con el Loc del nodo).
//    Un nodo que viene de un import usa un DILexicalBlockFile para que lldb muestre su archivo.
//  - DILocalVariable para variables locales y parámetros (llvm.dbg.declare / debug records).
//  - Tipos: int, float y bool son DIBasicType; struct, Option[T] y fn(...) son DICompositeType con
//    su nombre (struct con sus campos); str, List, Dict y Tensor son punteros a tipos opacos.
//    Ver docs/referencia/depuracion.md para las limitaciones.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ast/ast.h"
#include "sema/types.h"

#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"

namespace lg {

class DebugInfo {
public:
  // Añade los flags del módulo y el DICompileUnit. `prog` aporta archivo principal y structs.
  DebugInfo(llvm::Module& mod, const Program& prog);
  ~DebugInfo();

  // Cierra los metadatos. Debe llamarse una vez, tras generar todas las funciones.
  void finalize();

  // Layout real de la caja de un Option[T] (la define codegen; su último elemento es el valor y el
  // penúltimo la etiqueta). Sin esta función, Option se describe como tipo opaco.
  void setOptionBox(std::function<llvm::StructType*(const Type*)> fn) { optionBox_ = std::move(fn); }

  // ---- ámbito de función (usar DbgFunctionScope, no llamar directamente)
  void beginFunction(llvm::Function* F, const std::string& name, const std::string& linkageName,
                     const FnDecl* fd, const Loc& loc);
  void endFunction();

  // ---- ubicación de instrucciones (usar DbgLocGuard)
  // loc.line == 0 limpia la ubicación (instrucciones sin línea, p. ej. auxiliares).
  void setLoc(llvm::IRBuilder<>& B, const Loc& loc);

  // Declara una variable local o parámetro cuya dirección es `addr`. Usa como línea y ámbito la
  // ubicación actual del constructor (B.getCurrentDebugLocation()). No hace nada sin ubicación.
  void declareVariable(llvm::IRBuilder<>& B, const std::string& name, const Type* t, llvm::Value* addr,
                       bool isParam);

private:
  struct FnState {
    llvm::DISubprogram* sp = nullptr;
    unsigned argNo = 0;
  };

  llvm::DIFile* fileFor(const std::string& path);
  llvm::DIScope* scopeFor(const Loc& loc);
  llvm::DIType* typeOf(const Type* t);   // tipo visible en una variable (punteros para heap)
  llvm::DICompositeType* baseOf(const Type* t);  // tipo al que apunta un valor heap
  llvm::DISubroutineType* signatureOf(const FnDecl* fd, const Type* retType);
  llvm::DIBasicType* basic(const std::string& name, uint64_t bits, unsigned enc);

  llvm::Module& mod_;
  llvm::LLVMContext& ctx_;
  std::unique_ptr<llvm::DIBuilder> dbg_;
  llvm::DICompileUnit* cu_ = nullptr;
  std::map<std::string, llvm::DIFile*> files_;
  std::map<const Type*, llvm::DIType*> types_;   // tipo visible (pointer para heap)
  std::map<const Type*, llvm::DICompositeType*> bases_;  // tipo base de los valores heap
  std::set<const Type*> inProgress_;             // tipos base en construcción (recursión)
  std::map<std::string, const StructDecl*> structs_;
  std::function<llvm::StructType*(const Type*)> optionBox_;
  std::vector<FnState> stack_;
  bool finalized_ = false;
};

// Ubicación de depuración para las instrucciones creadas en su ámbito. Restaura la anterior al salir.
// Con dbg == nullptr no hace nada: sin -g el IR es idéntico al de antes.
class DbgLocGuard {
public:
  DbgLocGuard(DebugInfo* dbg, llvm::IRBuilder<>& B, const Loc& loc)
      : dbg_(dbg), B_(B), saved_(B.getCurrentDebugLocation()) {
    if (dbg_) dbg_->setLoc(B_, loc);
  }
  ~DbgLocGuard() {
    if (dbg_) B_.SetCurrentDebugLocation(saved_);
  }
  DbgLocGuard(const DbgLocGuard&) = delete;
  DbgLocGuard& operator=(const DbgLocGuard&) = delete;

private:
  DebugInfo* dbg_;
  llvm::IRBuilder<>& B_;
  llvm::DebugLoc saved_;
};

// Ámbito de una función generada: crea su DISubprogram y lo cierra al salir.
class DbgFunctionScope {
public:
  DbgFunctionScope(DebugInfo* dbg, llvm::Function* F, const std::string& name,
                   const std::string& linkageName, const FnDecl* fd, const Loc& loc)
      : dbg_(dbg) {
    if (dbg_) dbg_->beginFunction(F, name, linkageName, fd, loc);
  }
  ~DbgFunctionScope() {
    if (dbg_) dbg_->endFunction();
  }
  DbgFunctionScope(const DbgFunctionScope&) = delete;
  DbgFunctionScope& operator=(const DbgFunctionScope&) = delete;

private:
  DebugInfo* dbg_;
};

}  // namespace lg
