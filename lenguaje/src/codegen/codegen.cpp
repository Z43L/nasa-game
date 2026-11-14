// Codegen: AST anotado por sema -> módulo LLVM -> objeto nativo.
//
// MODELO DE MEMORIA (regla clave)
//  - Toda expresión produce un valor POSEÍDO (+1) si su tipo es heap (Str, List, Dict, Tensor, Struct).
//  - Ident: retain (el valor de la variable sigue perteneciendo a la variable).
//  - Almacenar (variable, campo, elemento, argumento de lg_list_append...) TRANSFIERE la posesión.
//  - Un temporal que no se almacena se libera justo después de usarlo (releaseIf).
//  - Parámetros: prestados. Dentro de la función no se liberan; reasignar un parámetro heap es error.
//  - Funciones: devuelven valores poseídos. El llamador libera los argumentos temporales tras la llamada.
//  - Variables locales heap: se liberan al salir del ámbito (normal, break, continue, return).
//    Al liberar se pone el alloca a null; los allocas se inicializan a null en la entrada de la función,
//    así que release(null) (no-op en runtime) es seguro aunque la variable no se haya declarado aún.
//  - lg_list_get / lg_dict_get_or_panic NO retienen: el resultado se retiene al extraerlo.
//  - lg_list_pop devuelve la posesión: no se retiene.
//  - Structs: lg_object_new(size, destroy_<Nombre>). El destroy libera los campos heap; la memoria
//    del objeto la libera el runtime (lg_release / lg_weak_release), no el destroy.
//
// REPRESENTACIÓN
//  - Int -> i64, Float -> double, Bool -> i1 en SSA e i8 en memoria (allocas y campos).
//  - Heap -> ptr. Cabecera LLVM {i64 rc, ptr destroy, i64 weak} = LgHeader (24 bytes, ABI v2);
//    todo objeto heap del codegen (struct, Option, celda, clausura, Debil) empieza por ella.
//    Struct: cabecera + campos.
//  - Option[T]: caja en el heap {cabecera, i64 etiqueta, T valor}. Etiqueta 0 = None
//    (valor a cero), 1 = Some. Al ser heap, la posesión sigue las mismas reglas que el resto.
//  - Clausura (valor de tipo fn(...)): heap {cabecera, ptr función, captura_0, ...}.
//    La función generada recibe la clausura como primer argumento (self) y lee las capturas de ella.
//    Captura inmutable: copia del valor (con retain). Captura `mut`: puntero a una CELDA del heap
//    {cabecera, T valor}; la variable del ámbito externo también vive en esa celda,
//    así las dos partes comparten el valor.
//  - extern "C": bool se pasa como i8 en la frontera (zext/trunc).
//  - Slots de listas/dicts: int directo, float bitcast a i64, bool zext, heap ptrtoint.
//
// DECISIONES / SUPUESTOS (ver informe)
//  - Panics de overflow y división por cero: lg_panic_overflow(file, line, op) y
//    lg_panic_div_zero(file, line) se declaran como externas; el runtime debe implementarlas
//    como noreturn (el codegen emite `unreachable` tras llamarlas).
//  - == de structs: llama al método `eq(self, other) -> bool` si existe; si no, compara identidad.
//  - Strings dentro de contenedores/structs se imprimen entre comillas (print de str suelto no).
//  - Argumentos con nombre: se reordenan por nombre de parámetro / campo.
//  - Builtins zeros/ones/randn/from_list: las dimensiones son argumentos enteros posicionales
//    (zeros(2, 3)) o una única lista (zeros([2, 3])). La forma (rank) sale de los argumentos o del tipo.
//  - Dentro de main, las sentencias de nivel superior son locales de main (no hay globales).
//  - Los métodos builtin se despachan por el nombre del miembro (Member::name).
//  - lg_dict_set toma posesión del valor; para claves str la entrada retiene la clave solo si es
//    nueva, así que el codegen libera siempre su propia referencia a la clave tras la llamada.
//    lg_list_set libera el valor anterior (lg_rt.h).
//  - lg_tensor_from_list toma la lista prestada (el codegen la libera después).

#include "codegen/codegen.h"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "runtime/lg_rt.h"
#include "codegen/debug_info.h"

#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"

namespace L = llvm;

namespace lg {
namespace {

// Lanzada por fail() para abortar la generación tras registrar el diagnóstico.
struct CgAbort {};

struct VarInfo {
  // Dirección del valor: un alloca, un GEP sobre la clausura (captura inmutable) o, si `cell`,
  // un alloca que guarda el puntero a la celda (la dirección del valor es el campo 2 de la celda).
  L::Value* slot = nullptr;
  const Type* type = nullptr;
  bool owned = false;   // el ámbito posee una referencia y la libera al salir
  bool cell = false;    // la variable vive en una celda compartida (captura por referencia)
  bool param = false;   // parámetro: prestado, no se reasigna si es heap
};

struct Scope {
  std::vector<VarInfo> vars;             // orden de declaración (para liberar en orden inverso)
  std::map<std::string, VarInfo> names;  // búsqueda por nombre
};

struct LoopCtx {
  L::BasicBlock* breakBB;
  L::BasicBlock* continueBB;
  size_t breakDepth;     // scopes a liberar al hacer break (índice del primer scope)
  size_t continueDepth;  // scopes a liberar al hacer continue
};

class CodeGen {
public:
  CodeGen(const Program& prog, const TypeTable& types, Diagnostics& diags, L::LLVMContext& ctx,
          L::Module& mod)
      : prog_(prog), types_(types), diags_(diags), ctx_(ctx), mod_(mod), B_(ctx) {}
  void setDebug(DebugInfo* dbg) { dbg_ = dbg; }
  // --export (solo wasm): nombres de funciones de nivel superior que se exportan con ese mismo nombre.
  void setExports(const std::vector<std::string>& names) { exports_ = names; }
  L::StructType* optionBoxType(const Type* t) { return optTy(t); }

  void run() {
    // 1. Structs: tipo LLVM, destroy_<Nombre> (declaración).
    for (const auto& d : prog_.decls) {
      if (d->kind != DeclKind::Struct) continue;
      const auto& sd = static_cast<const StructDecl&>(*d);
      structDecls_[sd.name] = &sd;
      std::vector<L::Type*> elems = {I64(), P(), I64()};
      for (const auto& f : sd.fields) elems.push_back(memTy(fieldType(f, sd.loc)));
      structTys_[sd.name] = L::StructType::create(ctx_, elems, "struct." + sd.name);
      destroyFns_[sd.name] = L::Function::Create(L::FunctionType::get(VOID(), {P()}, false),
                                                 L::Function::ExternalLinkage,
                                                 "destroy_" + sd.name, mod_);
    }
    // 2. Índice de métodos por tipo.
    for (const auto& d : prog_.decls) {
      if (d->kind != DeclKind::Impl) continue;
      const auto& im = static_cast<const ImplDecl&>(*d);
      for (const auto& m : im.methods) methods_[im.typeName][m->name] = m.get();
    }
    // 2b. Globales mutables de nivel superior usadas por funciones (sema: VarDecl::isGlobal). Su
    // valor inicial es cero/null; la inicialización es la sentencia del programa principal.
    for (const auto& s : prog_.stmts) {
      if (!s || s->kind != StmtKind::VarDecl) continue;
      const auto& vd = static_cast<const VarDecl&>(*s);
      if (!vd.isGlobal) continue;
      if (!vd.type) fail(vd.loc, "global sin tipo (sema)");
      L::Type* mt = memTy(vd.type);
      auto* gv = new L::GlobalVariable(mod_, mt, false, L::GlobalValue::InternalLinkage,
                                       L::Constant::getNullValue(mt), "gm_" + vd.name);
      VarInfo vi;
      vi.slot = gv;
      vi.type = vd.type;
      globals_[vd.name] = vi;
    }
    // 3. Declaraciones de funciones (todas antes de los cuerpos: permiten llamadas hacia delante).
    for (const auto& d : prog_.decls) {
      if (d->kind == DeclKind::Fn) declareFn(static_cast<const FnDecl&>(*d));
      if (d->kind == DeclKind::Impl)
        for (const auto& m : static_cast<const ImplDecl&>(*d).methods) declareFn(*m);
    }
    // 4. Cuerpos.
    for (const auto& d : prog_.decls) {
      if (d->kind == DeclKind::Fn) {
        const auto& fd = static_cast<const FnDecl&>(*d);
        if (!fd.isExtern && fd.hasBody) genFunction(fd);
      } else if (d->kind == DeclKind::Impl) {
        for (const auto& m : static_cast<const ImplDecl&>(*d).methods) genFunction(*m);
      }
    }
    // 5. destroy_<Nombre>.
    for (const auto& kv : structDecls_) genDestroy(*kv.second);
    // 6. main.
    genMain();
    // 7. Envoltorios de exportación (--export).
    for (const auto& name : exports_) genExport(name);
  }

private:
  const Program& prog_;
  const TypeTable& types_;
  Diagnostics& diags_;
  L::LLVMContext& ctx_;
  L::Module& mod_;
  L::IRBuilder<> B_;

  std::map<std::string, const StructDecl*> structDecls_;
  std::map<std::string, L::StructType*> structTys_;
  std::map<std::string, L::Function*> destroyFns_;
  std::map<std::string, const FnDecl*> fnDecls_;  // nombre LLVM -> declaración
  std::map<std::string, std::map<std::string, const FnDecl*>> methods_;

  std::vector<Scope> scopes_;
  std::map<std::string, VarInfo> globals_;  // globales mut de nivel superior (F0), por nombre
  std::vector<LoopCtx> loops_;
  L::Function* curFn_ = nullptr;
  L::UncondBrInst* anchor_ = nullptr;  // allocas se insertan antes de este br (fin del bloque entry)
  const Type* curRetType_ = nullptr;
  bool inMain_ = false;
  std::map<const Type*, L::Function*> cellDestroys_;  // destroy de celdas, por tipo del valor
  std::map<const Type*, L::Function*> optDestroys_;   // destroy de cajas Option, por tipo Option
  std::map<const FnDecl*, L::Function*> cloDestroys_; // destroy de clausuras, por función anidada
  std::map<const FnDecl*, L::Function*> hiloWraps_;   // envoltorio de hilo por función lanzada (hilo / async)
  DebugInfo* dbg_ = nullptr;  // información DWARF (lgc -g); nullptr = sin depuración
  std::vector<std::string> exports_;  // --export: nombres de funciones exportadas (wasm)

  // ------------------------------------------------------------------ utilidades básicas

  [[noreturn]] void fail(const Loc& loc, const std::string& msg, const std::string& hint = "") {
    diags_.error(loc, msg, hint);
    throw CgAbort{};
  }

  L::Type* VOID() { return L::Type::getVoidTy(ctx_); }
  L::Type* I1() { return L::Type::getInt1Ty(ctx_); }
  L::Type* I8() { return L::Type::getInt8Ty(ctx_); }
  L::Type* I32() { return L::Type::getInt32Ty(ctx_); }
  L::Type* I64() { return L::Type::getInt64Ty(ctx_); }
  L::Type* F64() { return L::Type::getDoubleTy(ctx_); }
  L::Type* P() { return L::PointerType::getUnqual(ctx_); }

  bool terminated() {
    L::BasicBlock* bb = B_.GetInsertBlock();
    return bb && !bb->empty() && bb->back().isTerminator();
  }

  L::BasicBlock* newBB(const std::string& name) {
    return L::BasicBlock::Create(ctx_, name, curFn_);
  }

  L::Value* cstr(const std::string& s) { return B_.CreateGlobalString(L::StringRef(s)); }

  // Llamada a función externa (runtime o C), declarada una sola vez en el módulo.
  L::Value* rc(const std::string& name, L::Type* ret, const std::vector<L::Type*>& ps,
               const std::vector<L::Value*>& args) {
    L::FunctionCallee f = mod_.getOrInsertFunction(name, L::FunctionType::get(ret, ps, false));
    return B_.CreateCall(f, args);
  }

  // Alloca en el bloque de entrada, inicializada a cero/null (ver nota de memoria arriba).
  L::AllocaInst* createEntryAlloca(L::Type* ty, const std::string& name) {
    L::IRBuilder<> AB(ctx_);
    AB.SetInsertPoint(anchor_);
    L::AllocaInst* a = AB.CreateAlloca(ty, nullptr, name);
    AB.CreateStore(L::Constant::getNullValue(ty), a);
    return a;
  }

  const Type* fieldType(const FieldDecl& f, const Loc& loc) {
    if (!f.type) fail(loc, "campo '" + f.name + "' sin tipo (sema)");
    return f.type;
  }

  const Type* typeOf(const Expr& e) {
    if (!e.type || e.type->kind == TypeKind::Error) fail(e.loc, "expresión sin tipo válido (sema)");
    return e.type;
  }

  // Tipo LLVM de un valor SSA.
  L::Type* ssaTy(const Type* t, const Loc& loc) {
    switch (t->kind) {
      case TypeKind::Int: return I64();
      case TypeKind::Float: return F64();
      case TypeKind::Bool: return I1();
      case TypeKind::Void: return VOID();
      case TypeKind::Str:
      case TypeKind::List:
      case TypeKind::Dict:
      case TypeKind::Tensor:
      case TypeKind::Struct:
      case TypeKind::Option:
      case TypeKind::Func:
      case TypeKind::Hilo:
      case TypeKind::Canal:
      case TypeKind::Debil: return P();
      default: break;
    }
    fail(loc, "tipo no soportado en codegen: " + t->str());
  }

  // Tipo LLVM en memoria (allocas y campos): Bool ocupa i8.
  L::Type* memTy(const Type* t) {
    if (t->kind == TypeKind::Bool) return I8();
    return ssaTy(t, Loc{});
  }

  // Tipo LLVM en la frontera C (extern "C"): Bool es i8.
  L::Type* cabiTy(const Type* t, const Loc& loc) {
    if (t->kind == TypeKind::Bool) return I8();
    return ssaTy(t, loc);
  }

  L::Value* toMem(L::Value* v, const Type* t) {
    return t->kind == TypeKind::Bool ? B_.CreateZExt(v, I8()) : v;
  }
  L::Value* fromMem(L::Value* v, const Type* t) {
    return t->kind == TypeKind::Bool ? B_.CreateTrunc(v, I1()) : v;
  }

  // Conversión a/desde slot de 64 bits (listas y diccionarios).
  L::Value* toSlot(L::Value* v, const Type* t) {
    switch (t->kind) {
      case TypeKind::Int: return v;
      case TypeKind::Float: return B_.CreateBitCast(v, I64());
      case TypeKind::Bool: return B_.CreateZExt(v, I64());
      default: return B_.CreatePtrToInt(v, I64());
    }
  }
  L::Value* fromSlot(L::Value* s, const Type* t) {
    switch (t->kind) {
      case TypeKind::Int: return s;
      case TypeKind::Float: return B_.CreateBitCast(s, F64());
      case TypeKind::Bool: return B_.CreateTrunc(s, I1());
      default: return B_.CreateIntToPtr(s, P());
    }
  }

  // Libera una referencia heap sin comprobar el tipo (null es no-op en el runtime).
  void releaseRaw(L::Value* v) { rc("lg_release", VOID(), {P()}, {v}); }

  // Genera el cuerpo de una función auxiliar (destroy) sin perder el punto de inserción actual.
  L::Function* makeHelper(const std::string& name, const std::function<void(L::Value*)>& body) {
    L::Function* F = L::Function::Create(L::FunctionType::get(VOID(), {P()}, false),
                                         L::Function::InternalLinkage, name, mod_);
    DbgLocGuard noLoc(dbg_, B_, Loc{});  // función auxiliar sin ubicación de depuración
    L::BasicBlock* saved = B_.GetInsertBlock();
    B_.SetInsertPoint(L::BasicBlock::Create(ctx_, "entry", F));
    body(F->getArg(0));
    B_.CreateRetVoid();
    if (saved) B_.SetInsertPoint(saved);
    return F;
  }

  // Tipos de cajas heap: celda de variable mut capturada, caja Option y clausura.
  L::StructType* cellTy(const Type* t) { return L::StructType::get(ctx_, {I64(), P(), I64(), memTy(t)}); }
  L::StructType* optTy(const Type* ot) { return L::StructType::get(ctx_, {I64(), P(), I64(), I64(), memTy(ot->elem)}); }
  L::StructType* cloTy(const FnDecl& fd) {
    std::vector<L::Type*> el = {I64(), P(), I64(), P()};
    for (const Capture& c : fd.captures) el.push_back(c.byRef ? P() : memTy(c.type));
    return L::StructType::get(ctx_, el);
  }
  uint64_t allocSize(L::StructType* st) { return mod_.getDataLayout().getTypeAllocSize(st).getFixedValue(); }

  L::Function* cellDestroy(const Type* t) {
    auto it = cellDestroys_.find(t);
    if (it != cellDestroys_.end()) return it->second;
    L::Function* F = makeHelper("destroy_cell_" + std::to_string(cellDestroys_.size()), [&](L::Value* obj) {
      if (t->isHeap()) {
        L::Value* fp = B_.CreateStructGEP(cellTy(t), obj, 3);
        releaseIf(fromMem(B_.CreateLoad(memTy(t), fp), t), t);
      }
    });
    cellDestroys_[t] = F;
    return F;
  }

  L::Function* optDestroy(const Type* ot) {
    auto it = optDestroys_.find(ot);
    if (it != optDestroys_.end()) return it->second;
    const Type* et = ot->elem;
    L::Function* F = makeHelper("destroy_option_" + std::to_string(optDestroys_.size()), [&](L::Value* obj) {
      if (et->isHeap()) {
        L::Value* fp = B_.CreateStructGEP(optTy(ot), obj, 4);
        releaseIf(fromMem(B_.CreateLoad(memTy(et), fp), et), et);
      }
    });
    optDestroys_[ot] = F;
    return F;
  }

  // Caja Option: etiqueta 0 = None (valor a cero), 1 = Some(v). Transfiere la posesión de v.
  L::Value* allocOption(const Type* ot, bool some, L::Value* v) {
    L::StructType* st = optTy(ot);
    L::Value* o = rc("lg_object_new", P(), {I64(), P()}, {B_.getInt64(allocSize(st)), optDestroy(ot)});
    B_.CreateStore(B_.getInt64(some ? 1 : 0), B_.CreateStructGEP(st, o, 3));
    L::Value* val = v ? toMem(v, ot->elem) : L::Constant::getNullValue(memTy(ot->elem));
    B_.CreateStore(val, B_.CreateStructGEP(st, o, 4));
    return o;
  }

  // Dirección del valor de una variable (ver VarInfo::slot).
  L::Value* addrOf(const VarInfo& vi) {
    if (!vi.cell) return vi.slot;
    L::Value* cp = B_.CreateLoad(P(), vi.slot);
    return B_.CreateStructGEP(cellTy(vi.type), cp, 3);
  }

  // Crea una celda con el valor v (posesión transferida) y devuelve su puntero (rc = 1).
  L::Value* newCell(const Type* t, L::Value* v) {
    L::StructType* st = cellTy(t);
    L::Value* cell = rc("lg_object_new", P(), {I64(), P()}, {B_.getInt64(allocSize(st)), cellDestroy(t)});
    if (v) B_.CreateStore(toMem(v, t), B_.CreateStructGEP(st, cell, 3));
    return cell;
  }

  void retainIf(L::Value* v, const Type* t) {
    if (v && t && t->isHeap()) rc("lg_retain", VOID(), {P()}, {v});
  }
  void releaseIf(L::Value* v, const Type* t) {
    if (v && t && t->isHeap()) rc("lg_release", VOID(), {P()}, {v});
  }

  // ------------------------------------------------------------------ ámbitos y variables

  void pushScope() { scopes_.emplace_back(); }

  void releaseScopeVars(const Scope& s) {
    for (auto it = s.vars.rbegin(); it != s.vars.rend(); ++it) {
      if (!it->owned) continue;
      if (it->cell) {  // la variable tiene una referencia a su celda
        releaseRaw(B_.CreateLoad(P(), it->slot));
        B_.CreateStore(L::Constant::getNullValue(P()), it->slot);
        continue;
      }
      if (!it->type->isHeap()) continue;
      L::Value* v = fromMem(B_.CreateLoad(memTy(it->type), it->slot), it->type);
      releaseIf(v, it->type);
      B_.CreateStore(L::Constant::getNullValue(memTy(it->type)), it->slot);
    }
  }

  // Cierra el ámbito actual: libera sus variables (si el bloque sigue abierto) y lo quita.
  void popScopeRelease() {
    if (!terminated()) releaseScopeVars(scopes_.back());
    scopes_.pop_back();
  }

  // Libera los ámbitos desde `depth` hasta el más interno (sin quitarlos): para break/continue/return.
  void releaseFrom(size_t depth) {
    for (size_t i = scopes_.size(); i-- > depth;) releaseScopeVars(scopes_[i]);
  }

  // asCell: la variable vive en una celda compartida (captura por referencia). owned siempre es true.
  VarInfo declareVar(const std::string& name, const Type* t, L::Value* v, bool owned, bool asCell = false,
                     bool isParam = false) {
    VarInfo vi;
    vi.type = t;
    vi.param = isParam;
    if (asCell) {
      L::AllocaInst* a = createEntryAlloca(P(), name);  // guarda el puntero a la celda
      if (isParam) retainIf(v, t);  // el parámetro es prestado: la celda toma su propia referencia
      B_.CreateStore(newCell(t, v), a);
      vi.slot = a;
      vi.owned = true;
      vi.cell = true;
    } else {
      L::AllocaInst* a = createEntryAlloca(memTy(t), name);
      if (v) B_.CreateStore(toMem(v, t), a);
      vi.slot = a;
      vi.owned = owned;
    }
    if (dbg_) dbg_->declareVariable(B_, name, t, addrOf(vi), isParam);
    scopes_.back().vars.push_back(vi);
    if (!name.empty()) scopes_.back().names[name] = vi;
    return vi;
  }

  L::Value* loadVar(const VarInfo& vi) {
    return fromMem(B_.CreateLoad(memTy(vi.type), addrOf(vi)), vi.type);
  }

  VarInfo lookup(const std::string& name, const Loc& loc) {
    for (size_t i = scopes_.size(); i-- > 0;) {
      auto it = scopes_[i].names.find(name);
      if (it != scopes_[i].names.end()) return it->second;
    }
    auto git = globals_.find(name);  // global mut de nivel superior (desde cualquier función)
    if (git != globals_.end()) return git->second;
    fail(loc, "variable no declarada: '" + name + "'");
  }

  // ------------------------------------------------------------------ tipos de struct y campos

  const StructDecl* structDecl(const std::string& name, const Loc& loc) {
    auto it = structDecls_.find(name);
    if (it == structDecls_.end()) fail(loc, "struct desconocida: " + name);
    return it->second;
  }

  L::StructType* structTy(const std::string& name, const Loc& loc) {
    auto it = structTys_.find(name);
    if (it == structTys_.end()) fail(loc, "struct desconocida: " + name);
    return it->second;
  }

  size_t fieldIndex(const StructDecl& sd, const std::string& field, const Loc& loc) {
    for (size_t j = 0; j < sd.fields.size(); ++j)
      if (sd.fields[j].name == field) return j;
    fail(loc, "la struct '" + sd.name + "' no tiene el campo '" + field + "'");
  }

  // Dirección del campo j de un objeto struct (cabecera en los índices 0 y 1).
  L::Value* fieldPtr(L::Value* obj, const Type* st, const std::string& field, const Type** ft,
                     const Loc& loc) {
    const StructDecl* sd = structDecl(st->name, loc);
    size_t j = fieldIndex(*sd, field, loc);
    *ft = fieldType(sd->fields[j], loc);
    return B_.CreateStructGEP(structTy(st->name, loc), obj, static_cast<unsigned>(j + 3));
  }

  // ------------------------------------------------------------------ declaraciones y cuerpos

  std::string fnName(const FnDecl& fd) { return fd.mangled.empty() ? fd.name : fd.mangled; }

  void declareFn(const FnDecl& fd) {
    std::string name = fnName(fd);
    if (mod_.getFunction(name)) fail(fd.loc, "función duplicada: " + name);
    std::vector<L::Type*> ps;
    for (const auto& p : fd.params) {
      if (!p.type) fail(p.loc, "parámetro '" + p.name + "' sin tipo (sema)");
      ps.push_back(fd.isExtern ? cabiTy(p.type, p.loc) : ssaTy(p.type, p.loc));
    }
    L::Type* rt = VOID();
    if (fd.retType && fd.retType->kind != TypeKind::Void)
      rt = fd.isExtern ? cabiTy(fd.retType, fd.loc) : ssaTy(fd.retType, fd.loc);
    L::Function::Create(L::FunctionType::get(rt, ps, false), L::Function::ExternalLinkage, name, mod_);
    fnDecls_[name] = &fd;
  }

  void genFunction(const FnDecl& fd) {
    L::Function* F = mod_.getFunction(fnName(fd));
    curFn_ = F;
    DbgFunctionScope dfs(dbg_, F, fd.implOf.empty() ? fd.name : fd.implOf + "." + fd.name, fnName(fd), &fd, fd.loc);
    L::BasicBlock* entry = L::BasicBlock::Create(ctx_, "entry", F);
    L::BasicBlock* body = L::BasicBlock::Create(ctx_, "body", F);
    anchor_ = L::UncondBrInst::Create(body, entry);
    B_.SetInsertPoint(body);
    scopes_.clear();
    loops_.clear();
    curRetType_ = fd.retType;
    inMain_ = false;

    pushScope();
    for (size_t i = 0; i < fd.params.size(); ++i) {
      const Param& p = fd.params[i];
      if (!p.type) fail(p.loc, "parámetro sin tipo (sema)");
      DbgLocGuard pl(dbg_, B_, p.loc);
      // Prestado; un `mut self` capturado por una función anidada vive en una celda.
      declareVar(p.name, p.type, F->getArg(static_cast<unsigned>(i)), false, p.captured, true);
    }
    for (const auto& s : fd.body) emitStmt(*s);
    popScopeRelease();
    scopes_.clear();
    finalizeBlocks(F);
  }

  // Bloques sin terminador (código muerto o caída al final): ret por defecto.
  void finalizeBlocks(L::Function* F) {
    for (auto& bb : *F) {
      if (!bb.empty() && bb.back().isTerminator()) continue;
      B_.SetInsertPoint(&bb);
      if (F->getReturnType()->isVoidTy()) B_.CreateRetVoid();
      else B_.CreateRet(L::Constant::getNullValue(F->getReturnType()));
    }
  }

  void genDestroy(const StructDecl& sd) {
    L::Function* F = destroyFns_[sd.name];
    B_.SetInsertPoint(L::BasicBlock::Create(ctx_, "entry", F));
    L::Value* obj = F->getArg(0);
    L::StructType* sty = structTy(sd.name, sd.loc);
    for (size_t j = 0; j < sd.fields.size(); ++j) {
      const Type* ft = fieldType(sd.fields[j], sd.loc);
      if (!ft->isHeap()) continue;
      L::Value* fp = B_.CreateStructGEP(sty, obj, static_cast<unsigned>(j + 3));
      releaseIf(B_.CreateLoad(P(), fp), ft);
    }
    B_.CreateRetVoid();
  }

  // Tipo de un parámetro o retorno exportado a JS. Frontera wasm: int = i64 (BigInt en JS),
  // float = f64, bool = i32 (0/1), str (solo retorno) = puntero a LgStr (ver lg_wasm_ret_store).
  L::Type* exportTy(const Type* t, const std::string& fn, const Loc& loc, bool param) {
    switch (t->kind) {
      case TypeKind::Int: return I64();
      case TypeKind::Float: return F64();
      case TypeKind::Bool: return I32();
      case TypeKind::Str:
        if (!param) return P();
        break;
      default: break;
    }
    fail(loc, "--export: la función '" + fn + "' tiene un tipo no exportable (" + t->str() + ")",
         param ? "los parámetros exportados son int, float o bool" : "el retorno exportado es int, float, bool o str");
  }

  // Envoltorio exportado "lgx_<nombre>" con el atributo wasm-export-name = <nombre>: así el export
  // del .wasm tiene el nombre del .lg y no el mangled u_<nombre>. Convierte bool (i1 <-> i32) y,
  // para str, pasa el resultado (+1) a lg_wasm_ret_store, que lo guarda hasta la próxima exportación.
  void genExport(const std::string& name) {
    const FnDecl* fd = nullptr;
    for (const auto& d : prog_.decls) {
      if (d->kind != DeclKind::Fn) continue;
      const auto& cand = static_cast<const FnDecl&>(*d);
      if (cand.name == name) fd = &cand;
    }
    if (!fd || fd->isExtern || !fd->hasBody || fd->isAsync)
      fail(Loc{prog_.file, 1, 1}, "--export: no existe la función de nivel superior '" + name + "'",
           "solo se exportan funciones definidas en el programa (no extern, no async)");
    std::vector<L::Type*> ps;
    for (const auto& p : fd->params) ps.push_back(exportTy(p.type, name, p.loc, true));
    const bool retVoid = !fd->retType || fd->retType->kind == TypeKind::Void;
    L::Type* rt = retVoid ? VOID() : exportTy(fd->retType, name, fd->loc, false);
    L::Function* W = L::Function::Create(L::FunctionType::get(rt, ps, false), L::Function::ExternalLinkage,
                                         "lgx_" + name, mod_);
    W->addFnAttr("wasm-export-name", name);
    L::Function* F = mod_.getFunction(fnName(*fd));
    B_.SetInsertPoint(L::BasicBlock::Create(ctx_, "entry", W));
    std::vector<L::Value*> args;
    for (size_t i = 0; i < fd->params.size(); ++i) {
      L::Value* a = W->getArg(static_cast<unsigned>(i));
      if (fd->params[i].type->kind == TypeKind::Bool) a = B_.CreateTrunc(a, I1());
      args.push_back(a);
    }
    L::Value* r = B_.CreateCall(F, args);
    if (retVoid) {
      B_.CreateRetVoid();
    } else if (fd->retType->kind == TypeKind::Bool) {
      B_.CreateRet(B_.CreateZExt(r, I32()));
    } else if (fd->retType->kind == TypeKind::Str) {
      L::FunctionCallee store = mod_.getOrInsertFunction("lg_wasm_ret_store",
                                                         L::FunctionType::get(P(), {P()}, false));
      B_.CreateRet(B_.CreateCall(store, {r}));
    } else {
      B_.CreateRet(r);
    }
  }

  void genMain() {
    // En wasm32-wasip1 la entrada se llama __main_argc_argv (convención de wasi-libc, igual que
    // clang); con el nombre "main" crt1.o la tomaría como firma (void) y el enlace resulta roto.
    const char* entryName = mod_.getTargetTriple().getArch() == L::Triple::wasm32 ? "__main_argc_argv" : "main";
    L::Function* F = L::Function::Create(L::FunctionType::get(I32(), {I32(), P()}, false),
                                         L::Function::ExternalLinkage, entryName, mod_);
    curFn_ = F;
    DbgFunctionScope dfs(dbg_, F, "main", "main", nullptr, Loc{prog_.file, 1, 1});
    L::BasicBlock* entry = L::BasicBlock::Create(ctx_, "entry", F);
    L::BasicBlock* body = L::BasicBlock::Create(ctx_, "body", F);
    anchor_ = L::UncondBrInst::Create(body, entry);
    B_.SetInsertPoint(body);
    scopes_.clear();
    loops_.clear();
    curRetType_ = nullptr;
    inMain_ = true;

    pushScope();
    rc("lg_runtime_init", VOID(), {I32(), P()}, {F->getArg(0), F->getArg(1)});
    for (const auto& s : prog_.stmts) emitStmt(*s);
    popScopeRelease();
    scopes_.clear();
    finalizeBlocks(F);
  }

  // ------------------------------------------------------------------ sentencias

  void emitBlock(const std::vector<StmtPtr>& stmts) {
    pushScope();
    for (const auto& s : stmts) emitStmt(*s);
    popScopeRelease();
  }

  void emitStmt(const Stmt& s) {
    if (terminated()) B_.SetInsertPoint(newBB("dead"));  // código tras return/break/continue
    DbgLocGuard dloc(dbg_, B_, s.loc);
    switch (s.kind) {
      case StmtKind::VarDecl: return emitVarDecl(static_cast<const VarDecl&>(s));
      case StmtKind::Assign: return emitAssign(static_cast<const Assign&>(s));
      case StmtKind::ExprStmt: {
        const auto& es = static_cast<const ExprStmt&>(s);
        L::Value* v = emitExpr(*es.expr);
        if (v) releaseIf(v, typeOf(*es.expr));
        return;
      }
      case StmtKind::Return: return emitReturn(static_cast<const ReturnStmt&>(s));
      case StmtKind::If: return emitIfStmt(static_cast<const IfStmt&>(s));
      case StmtKind::For: return emitFor(static_cast<const ForStmt&>(s));
      case StmtKind::While: return emitWhile(static_cast<const WhileStmt&>(s));
      case StmtKind::Break: {
        if (loops_.empty()) fail(s.loc, "'break' fuera de un bucle");
        const LoopCtx& lc = loops_.back();
        releaseFrom(lc.breakDepth);
        B_.CreateBr(lc.breakBB);
        return;
      }
      case StmtKind::Continue: {
        if (loops_.empty()) fail(s.loc, "'continue' fuera de un bucle");
        const LoopCtx& lc = loops_.back();
        releaseFrom(lc.continueDepth);
        B_.CreateBr(lc.continueBB);
        return;
      }
      case StmtKind::Fn: return emitNestedFn(*static_cast<const FnStmt&>(s).fn);
    }
    fail(s.loc, "sentencia no soportada en codegen");
  }

  // Comprueba la forma de un tensor cuando el valor no la conoce en compilación.
  void checkShape(L::Value* v, const Type* from, const Type* to, const Loc& loc) {
    if (!from || !to || from->kind != TypeKind::Tensor || to->kind != TypeKind::Tensor) return;
    if (from->shapeKnown() || !to->shapeKnown()) return;
    L::ArrayType* at = L::ArrayType::get(I64(), to->shape.size());
    std::vector<L::Constant*> dims;
    for (int64_t d : to->shape) dims.push_back(L::ConstantInt::get(I64(), static_cast<uint64_t>(d)));
    L::AllocaInst* arr = createEntryAlloca(at, "shape");
    B_.CreateStore(L::ConstantArray::get(at, dims), arr);
    rc("lg_tensor_check_shape", VOID(), {P(), I64(), P(), P(), I64()},
       {v, B_.getInt64(to->shape.size()), arr, cstr(loc.file), B_.getInt64(static_cast<uint64_t>(loc.line))});
  }

  void emitVarDecl(const VarDecl& d) {
    if (!d.init) fail(d.loc, "declaración sin valor");
    const Type* initT = typeOf(*d.init);
    const Type* t = d.type ? d.type : initT;
    L::Value* v = emitExpr(*d.init);
    checkShape(v, initT, t, d.loc);
    if (d.isGlobal) {
      // La global vive fuera de los ámbitos: no se libera al salir de main (la posesión queda en ella).
      auto git = globals_.find(d.name);
      if (git == globals_.end()) fail(d.loc, "global sin símbolo (error interno): " + d.name);
      if (v) B_.CreateStore(toMem(v, t), git->second.slot);
      scopes_.back().names[d.name] = git->second;
      return;
    }
    declareVar(d.name, t, v, t->isHeap(), d.captured);
  }

  void emitAssign(const Assign& a) {
    if (a.declares) {
      if (a.target->kind != ExprKind::Ident) fail(a.loc, "declaración sobre algo que no es un nombre");
      const std::string& name = static_cast<const Ident&>(*a.target).name;
      const Type* t = typeOf(*a.value);
      L::Value* v = emitExpr(*a.value);
      declareVar(name, t, v, t->isHeap());
      return;
    }
    switch (a.target->kind) {
      case ExprKind::Ident: {
        const std::string& name = static_cast<const Ident&>(*a.target).name;
        VarInfo vi = lookup(name, a.loc);
        if (vi.type->isHeap() && vi.param)
          fail(a.loc, "no se puede reasignar el parámetro '" + name + "'",
               "copia el valor a una variable local");
        const Type* vt = typeOf(*a.value);
        L::Value* v = emitExpr(*a.value);
        checkShape(v, vt, vi.type, a.loc);
        L::Value* old = vi.type->isHeap() ? loadVar(vi) : nullptr;
        B_.CreateStore(toMem(v, vi.type), addrOf(vi));
        releaseIf(old, vi.type);  // se libera DESPUÉS de almacenar el nuevo
        return;
      }
      case ExprKind::Index: {
        const auto& ix = static_cast<const Index&>(*a.target);
        const Type* ot = typeOf(*ix.object);
        if (ot->kind == TypeKind::Tensor) {
          emitTensorSet(ix, a, ot);
          return;
        }
        L::Value* obj = emitExpr(*ix.object);
        const Type* it = typeOf(*ix.index);
        L::Value* idx = emitExpr(*ix.index);
        L::Value* v = emitExpr(*a.value);
        if (ot->kind == TypeKind::List) {
          rc("lg_list_set", VOID(), {P(), I64(), I64()}, {obj, idx, toSlot(v, ot->elem)});
        } else if (ot->kind == TypeKind::Dict) {
          // El valor pasa a la posesión del diccionario; la clave str se retiene dentro solo si es nueva.
          rc("lg_dict_set", VOID(), {P(), I64(), I64()}, {obj, toSlot(idx, it), toSlot(v, ot->val)});
          releaseIf(idx, it);
        } else {
          fail(a.loc, "asignación por índice solo válida en listas y diccionarios");
        }
        releaseIf(obj, ot);
        return;
      }
      case ExprKind::Member: {
        const auto& m = static_cast<const Member&>(*a.target);
        const Type* ot = typeOf(*m.object);
        if (ot->kind != TypeKind::Struct) fail(a.loc, "acceso a campo sobre un valor que no es struct");
        L::Value* obj = emitExpr(*m.object);
        L::Value* v = emitExpr(*a.value);
        const Type* ft = nullptr;
        L::Value* fp = fieldPtr(obj, ot, m.name, &ft, a.loc);
        L::Value* old = ft->isHeap() ? fromMem(B_.CreateLoad(memTy(ft), fp), ft) : nullptr;
        B_.CreateStore(toMem(v, ft), fp);
        releaseIf(old, ft);
        releaseIf(obj, ot);
        return;
      }
      default: break;
    }
    fail(a.loc, "destino de asignación no válido");
  }

  void emitReturn(const ReturnStmt& r) {
    if (inMain_) fail(r.loc, "'return' fuera de una función");
    L::Value* v = nullptr;
    if (r.value) {
      v = emitExpr(*r.value);
      checkShape(v, typeOf(*r.value), curRetType_, r.loc);
    }
    releaseFrom(0);
    if (!curRetType_ || curRetType_->kind == TypeKind::Void) {
      B_.CreateRetVoid();
    } else {
      if (!v) fail(r.loc, "'return' sin valor en una función que devuelve " + curRetType_->str());
      B_.CreateRet(v);
    }
  }

  void emitIfStmt(const IfStmt& s) {
    L::Value* c = emitExpr(*s.cond);
    L::BasicBlock* thenBB = newBB("if.then");
    L::BasicBlock* elseBB = s.elseBody.empty() ? nullptr : newBB("if.else");
    L::BasicBlock* merge = newBB("if.end");
    B_.CreateCondBr(c, thenBB, elseBB ? elseBB : merge);
    B_.SetInsertPoint(thenBB);
    emitBlock(s.thenBody);
    if (!terminated()) B_.CreateBr(merge);
    if (elseBB) {
      B_.SetInsertPoint(elseBB);
      emitBlock(s.elseBody);
      if (!terminated()) B_.CreateBr(merge);
    }
    B_.SetInsertPoint(merge);
  }

  void emitWhile(const WhileStmt& w) {
    L::BasicBlock* header = newBB("while.cond");
    L::BasicBlock* body = newBB("while.body");
    L::BasicBlock* after = newBB("while.end");
    B_.CreateBr(header);
    B_.SetInsertPoint(header);
    L::Value* c = emitExpr(*w.cond);
    B_.CreateCondBr(c, body, after);
    B_.SetInsertPoint(body);
    size_t depth = scopes_.size();
    loops_.push_back({after, header, depth, depth});
    emitBlock(w.body);
    loops_.pop_back();
    if (!terminated()) B_.CreateBr(header);
    B_.SetInsertPoint(after);
  }

  void emitFor(const ForStmt& f) {
    if (f.iter->kind == ExprKind::Range) return emitForRange(f);
    return emitForList(f);
  }

  void emitForRange(const ForStmt& f) {
    const auto& r = static_cast<const RangeExpr&>(*f.iter);
    L::Value* start = emitExpr(*r.start);
    L::Value* end = emitExpr(*r.end);
    L::AllocaInst* cur = createEntryAlloca(I64(), "for.cur");
    B_.CreateStore(start, cur);
    L::BasicBlock* header = newBB("for.cond");
    L::BasicBlock* body = newBB("for.body");
    L::BasicBlock* latch = newBB("for.latch");
    L::BasicBlock* after = newBB("for.end");
    B_.CreateBr(header);

    B_.SetInsertPoint(header);
    B_.CreateCondBr(B_.CreateICmpSLT(B_.CreateLoad(I64(), cur), end), body, after);

    B_.SetInsertPoint(body);
    size_t depth = scopes_.size();
    loops_.push_back({after, latch, depth, depth});
    pushScope();
    const Type* vt = f.varType ? f.varType : types_.intT();
    declareVar(f.var, vt, B_.CreateLoad(I64(), cur), false);
    for (const auto& s : f.body) emitStmt(*s);
    popScopeRelease();
    loops_.pop_back();
    if (!terminated()) B_.CreateBr(latch);

    B_.SetInsertPoint(latch);
    B_.CreateStore(B_.CreateAdd(B_.CreateLoad(I64(), cur), B_.getInt64(1), "", false, true), cur);
    B_.CreateBr(header);
    B_.SetInsertPoint(after);
  }

  void emitForList(const ForStmt& f) {
    const Type* lt = typeOf(*f.iter);
    if (lt->kind != TypeKind::List) fail(f.loc, "'for' solo itera sobre rangos (a..b) o listas");
    L::Value* lst = emitExpr(*f.iter);  // poseído: lo libera el ámbito externo
    size_t depth = scopes_.size();
    pushScope();
    declareVar("", lt, lst, true);  // variable oculta: la lista
    L::AllocaInst* idx = createEntryAlloca(I64(), "for.idx");
    B_.CreateStore(B_.getInt64(0), idx);
    L::BasicBlock* header = newBB("forl.cond");
    L::BasicBlock* body = newBB("forl.body");
    L::BasicBlock* latch = newBB("forl.latch");
    L::BasicBlock* done = newBB("forl.done");
    L::BasicBlock* after = newBB("forl.end");
    B_.CreateBr(header);

    B_.SetInsertPoint(header);
    L::Value* n = rc("lg_list_len", I64(), {P()}, {lst});
    B_.CreateCondBr(B_.CreateICmpSLT(B_.CreateLoad(I64(), idx), n), body, done);

    B_.SetInsertPoint(body);
    loops_.push_back({after, latch, depth, depth + 1});
    pushScope();
    const Type* et = lt->elem;
    L::Value* slot = rc("lg_list_get", I64(), {P(), I64()}, {lst, B_.CreateLoad(I64(), idx)});
    L::Value* v = fromSlot(slot, et);
    retainIf(v, et);  // lg_list_get no retiene
    declareVar(f.var, et, v, et->isHeap());
    for (const auto& s : f.body) emitStmt(*s);
    popScopeRelease();
    loops_.pop_back();
    if (!terminated()) B_.CreateBr(latch);

    B_.SetInsertPoint(latch);
    B_.CreateStore(B_.CreateAdd(B_.CreateLoad(I64(), idx), B_.getInt64(1)), idx);
    B_.CreateBr(header);

    B_.SetInsertPoint(done);
    popScopeRelease();  // libera la lista oculta y quita el ámbito exterior
    B_.CreateBr(after);
    B_.SetInsertPoint(after);
  }

  // ------------------------------------------------------------------ expresiones

  L::Value* emitExpr(const Expr& e) {
    DbgLocGuard dloc(dbg_, B_, e.loc);
    switch (e.kind) {
      case ExprKind::IntLit:
        return B_.getInt64(static_cast<uint64_t>(static_cast<const IntLit&>(e).value));
      case ExprKind::FloatLit:
        return L::ConstantFP::get(F64(), static_cast<const FloatLit&>(e).value);
      case ExprKind::BoolLit:
        return B_.getInt1(static_cast<const BoolLit&>(e).value);
      case ExprKind::StrLit: {
        const auto& s = static_cast<const StrLit&>(e);
        return rc("lg_str_new", P(), {P(), I64()}, {cstrRaw(s.value), B_.getInt64(s.value.size())});
      }
      case ExprKind::Ident: {
        const auto& id = static_cast<const Ident&>(e);
        VarInfo vi = lookup(id.name, e.loc);
        L::Value* v = loadVar(vi);
        retainIf(v, vi.type);  // leer una variable produce una referencia propia
        return v;
      }
      case ExprKind::Unary: {
        const auto& u = static_cast<const Unary&>(e);
        const Type* t = typeOf(*u.operand);
        L::Value* v = emitExpr(*u.operand);
        if (u.op == UnaryOp::Not) {
          if (t->kind != TypeKind::Bool) fail(e.loc, "'not' requiere bool, recibido " + t->str());
          return B_.CreateNot(v);
        }
        if (t->kind == TypeKind::Int)
          return checkedArith(L::Intrinsic::ssub_with_overflow, B_.getInt64(0), v, e.loc, "-");
        if (t->kind == TypeKind::Float) return B_.CreateFNeg(v);
        fail(e.loc, "operador '-' no aplicable a " + t->str());
      }
      case ExprKind::Binary: return emitBinary(static_cast<const Binary&>(e));
      case ExprKind::Call: return emitCall(static_cast<const Call&>(e));
      case ExprKind::Await: return emitAwait(static_cast<const AwaitExpr&>(e));
      case ExprKind::Index: return emitIndex(static_cast<const Index&>(e));
      case ExprKind::Member: {
        const auto& m = static_cast<const Member&>(e);
        if (!m.isField) fail(e.loc, "método '" + m.name + "' usado sin llamarlo");
        const Type* ot = typeOf(*m.object);
        if (ot->kind != TypeKind::Struct) fail(e.loc, "acceso a campo sobre " + ot->str());
        L::Value* obj = emitExpr(*m.object);
        const Type* ft = nullptr;
        L::Value* fp = fieldPtr(obj, ot, m.name, &ft, e.loc);
        L::Value* v = fromMem(B_.CreateLoad(memTy(ft), fp), ft);
        retainIf(v, ft);  // el campo sigue siendo del struct: damos una referencia propia
        releaseIf(obj, ot);
        return v;
      }
      case ExprKind::ListLit: {
        const Type* t = typeOf(e);
        if (t->kind != TypeKind::List || !t->elem) fail(e.loc, "lista sin tipo de elemento");
        const auto& ll = static_cast<const ListLit&>(e);
        L::Value* l = rc("lg_list_new", P(), {I64(), I64()},
                         {B_.getInt64(t->elem->isHeap() ? 1 : 0), B_.getInt64(ll.elems.size())});
        for (const auto& el : ll.elems) {
          const Type* et = typeOf(*el);
          L::Value* v = emitExpr(*el);
          rc("lg_list_append", VOID(), {P(), I64()}, {l, toSlot(v, et)});  // transfiere la posesión
        }
        return l;
      }
      case ExprKind::DictLit: {
        const Type* t = typeOf(e);
        if (t->kind != TypeKind::Dict || !t->key || !t->val) fail(e.loc, "diccionario sin tipo");
        const auto& dl = static_cast<const DictLit&>(e);
        L::Value* d = rc("lg_dict_new", P(), {I64(), I64()},
                         {B_.getInt64(keyKind(t->key, e.loc)), B_.getInt64(t->val->isHeap() ? 1 : 0)});
        for (const auto& kv : dl.entries) {
          const Type* kt = typeOf(*kv.first);
          L::Value* k = emitExpr(*kv.first);
          const Type* vt = typeOf(*kv.second);
          L::Value* v = emitExpr(*kv.second);
          rc("lg_dict_set", VOID(), {P(), I64(), I64()}, {d, toSlot(k, kt), toSlot(v, vt)});
          // La clave str solo se retiene dentro si es nueva: liberamos nuestra referencia.
          releaseIf(k, kt);
        }
        return d;
      }
      case ExprKind::NoneLit: return allocOption(typeOf(e), false, nullptr);
      case ExprKind::Range: break;
    }
    if (e.kind == ExprKind::Range) fail(e.loc, "un rango solo es válido como iterable de 'for'");
    fail(e.loc, "expresión no soportada en codegen");
  }

  // Cadena literal con su longitud exacta (puede contener bytes nulos).
  L::Value* cstrRaw(const std::string& s) { return B_.CreateGlobalString(L::StringRef(s)); }

  int64_t keyKind(const Type* k, const Loc& loc) {
    switch (k->kind) {
      case TypeKind::Int: return LG_KEY_INT;
      case TypeKind::Str: return LG_KEY_STR;
      case TypeKind::Bool: return LG_KEY_BOOL;
      default: fail(loc, "clave de diccionario no soportada: " + k->str(), "usa int, str o bool");
    }
  }

  // Aritmética entera con comprobación de desbordamiento.
  L::Value* checkedArith(L::Intrinsic::ID id, L::Value* a, L::Value* b, const Loc& loc,
                         const std::string& op) {
    L::Function* f = L::Intrinsic::getOrInsertDeclaration(&mod_, id, std::vector<L::Type*>{I64()});
    L::Value* r = B_.CreateCall(f, {a, b});
    L::Value* val = B_.CreateExtractValue(r, {0});
    L::Value* ov = B_.CreateExtractValue(r, {1});
    L::BasicBlock* panicBB = newBB("ovf.panic");
    L::BasicBlock* contBB = newBB("ovf.cont");
    B_.CreateCondBr(ov, panicBB, contBB);
    B_.SetInsertPoint(panicBB);
    rc("lg_panic_overflow", VOID(), {P(), I64(), P()},
       {cstr(loc.file), B_.getInt64(static_cast<uint64_t>(loc.line)), cstr(op)});
    B_.CreateUnreachable();
    B_.SetInsertPoint(contBB);
    return val;
  }

  // División o módulo enteros con comprobación de cero y de INT64_MIN / -1.
  L::Value* checkedDiv(L::Value* a, L::Value* b, bool isRem, const Loc& loc) {
    std::string op = isRem ? "%" : "/";
    L::BasicBlock* zeroBB = newBB("div.zero");
    L::BasicBlock* nextBB = newBB("div.check");
    L::BasicBlock* ovBB = newBB("div.ovf");
    L::BasicBlock* divBB = newBB("div.ok");
    B_.CreateCondBr(B_.CreateICmpEQ(b, B_.getInt64(0)), zeroBB, nextBB);
    B_.SetInsertPoint(zeroBB);
    rc("lg_panic_div_zero", VOID(), {P(), I64()},
       {cstr(loc.file), B_.getInt64(static_cast<uint64_t>(loc.line))});
    B_.CreateUnreachable();
    B_.SetInsertPoint(nextBB);
    L::Value* isMin = B_.CreateICmpEQ(a, B_.getInt64(static_cast<uint64_t>(INT64_MIN)));
    L::Value* isM1 = B_.CreateICmpEQ(b, B_.getInt64(static_cast<uint64_t>(-1)));
    B_.CreateCondBr(B_.CreateAnd(isMin, isM1), ovBB, divBB);
    B_.SetInsertPoint(ovBB);
    rc("lg_panic_overflow", VOID(), {P(), I64(), P()},
       {cstr(loc.file), B_.getInt64(static_cast<uint64_t>(loc.line)), cstr(op)});
    B_.CreateUnreachable();
    B_.SetInsertPoint(divBB);
    return isRem ? B_.CreateSRem(a, b) : B_.CreateSDiv(a, b);
  }

  // float -> int con truncamiento hacia cero. Fuera de [-2^63, 2^63) o NaN es error de ejecución
  // (FPToSI con valor fuera de rango es comportamiento indefinido). NaN no pasa: las comparaciones
  // ordenadas (OGE/OLT) dan falso con NaN.
  L::Value* checkedFloatToInt(L::Value* v, const Loc& loc) {
    L::Value* lo = L::ConstantFP::get(F64(), -9.2233720368547758e18);  // -2^63, exacto
    L::Value* hi = L::ConstantFP::get(F64(), 9.2233720368547758e18);   // 2^63, exacto
    L::Value* inRange = B_.CreateAnd(B_.CreateFCmpOGE(v, lo), B_.CreateFCmpOLT(v, hi));
    L::BasicBlock* panicBB = newBB("f2i.panic");
    L::BasicBlock* contBB = newBB("f2i.cont");
    B_.CreateCondBr(inRange, contBB, panicBB);
    B_.SetInsertPoint(panicBB);
    rc("lg_panic_float_to_int", VOID(), {P(), I64(), F64()},
       {cstr(loc.file), B_.getInt64(static_cast<uint64_t>(loc.line)), v});
    B_.CreateUnreachable();
    B_.SetInsertPoint(contBB);
    return B_.CreateFPToSI(v, I64());
  }

  // opt == None / opt != None: compara la etiqueta, sin construir un None.
  L::Value* emitNoneCompare(const Binary& b) {
    const Expr& other = b.lhs->kind == ExprKind::NoneLit ? *b.rhs : *b.lhs;
    const Type* ot = typeOf(other);
    if (ot->kind != TypeKind::Option) fail(b.loc, "comparación con None solo válida sobre Option");
    L::Value* o = emitExpr(other);
    L::Value* tag = B_.CreateLoad(I64(), B_.CreateStructGEP(optTy(ot), o, 3));
    L::Value* isNone = B_.CreateICmpEQ(tag, B_.getInt64(0));
    releaseIf(o, ot);
    return b.op == BinaryOp::Eq ? isNone : B_.CreateNot(isNone);
  }

  L::Value* emitBinary(const Binary& b) {
    if (b.op == BinaryOp::And || b.op == BinaryOp::Or) return emitShortCircuit(b);
    if ((b.op == BinaryOp::Eq || b.op == BinaryOp::Ne) &&
        (b.lhs->kind == ExprKind::NoneLit || b.rhs->kind == ExprKind::NoneLit))
      return emitNoneCompare(b);
    const Type* lt = typeOf(*b.lhs);
    const Type* rt = typeOf(*b.rhs);
    L::Value* l = emitExpr(*b.lhs);
    L::Value* r = emitExpr(*b.rhs);
    const bool tensorScalar = (lt->kind == TypeKind::Tensor) != (rt->kind == TypeKind::Tensor) &&
                              (b.op == BinaryOp::Add || b.op == BinaryOp::Sub || b.op == BinaryOp::Mul ||
                               b.op == BinaryOp::Div);
    L::Value* res = tensorScalar                    ? emitTensorScalar(b.op, l, r, lt, rt, b.loc)
                    : b.op == BinaryOp::In          ? emitIn(l, r, lt, rt, b.loc)
                                                    : emitBinOp(b.op, l, r, lt, b.loc);
    releaseIf(l, lt);
    releaseIf(r, rt);
    return res;
  }

  // Tensor op escalar o escalar op Tensor (+ - * /): operandos prestados; el resultado es un tensor nuevo.
  L::Value* emitTensorScalar(BinaryOp op, L::Value* l, L::Value* r, const Type* lt, const Type* rt,
                             const Loc& loc) {
    const bool scalarLeft = lt->kind != TypeKind::Tensor;
    L::Value* t = scalarLeft ? r : l;
    L::Value* s = scalarLeft ? l : r;
    const Type* tt = scalarLeft ? rt : lt;
    int64_t code = 0;
    switch (op) {
      case BinaryOp::Add: code = 0; break;
      case BinaryOp::Sub: code = 1; break;
      case BinaryOp::Mul: code = 2; break;
      case BinaryOp::Div: code = 3; break;
      default: fail(loc, "operador no soportado entre tensor y escalar");
    }
    std::vector<L::Value*> args = {t, s, B_.getInt64(static_cast<uint64_t>(code)), B_.getInt64(scalarLeft ? 1 : 0),
                        cstr(loc.file), B_.getInt64(static_cast<uint64_t>(loc.line))};
    if (tt->elem->kind == TypeKind::Float)
      return rc("lg_tensor_scale_float", P(), {P(), F64(), I64(), I64(), P(), I64()}, args);
    return rc("lg_tensor_scale_int", P(), {P(), I64(), I64(), I64(), P(), I64()}, args);
  }

  // Evalúa los índices de t[i, j, ...] (en orden) en un arreglo local de int64 y devuelve su dirección.
  L::Value* emitIndexArray(const Index& ix) {
    std::vector<const Expr*> all{ix.index.get()};
    for (const auto& e : ix.rest) all.push_back(e.get());
    L::ArrayType* at = L::ArrayType::get(I64(), all.size());
    L::AllocaInst* arr = createEntryAlloca(at, "tidx");
    for (size_t k = 0; k < all.size(); k++) {
      L::Value* iv = emitExpr(*all[k]);
      B_.CreateStore(iv, B_.CreateConstInBoundsGEP1_64(I64(), arr, k));
    }
    return arr;
  }

  // Asignación t[i, j, ...] = v sobre un tensor (índice completo, comprobado en sema).
  void emitTensorSet(const Index& ix, const Assign& a, const Type* ot) {
    L::Value* obj = emitExpr(*ix.object);
    L::Value* arr = emitIndexArray(ix);
    L::Value* v = emitExpr(*a.value);
    std::vector<L::Value*> args = {obj, cstr(a.loc.file), B_.getInt64(static_cast<uint64_t>(a.loc.line)),
                        B_.getInt64(1 + ix.rest.size()), arr, v};
    if (ot->elem->kind == TypeKind::Float)
      rc("lg_tensor_set_float", VOID(), {P(), P(), I64(), I64(), P(), F64()}, args);
    else
      rc("lg_tensor_set_int", VOID(), {P(), P(), I64(), I64(), P(), I64()}, args);
    releaseIf(obj, ot);
  }

  // t[i, j, ...] sobre un tensor: elemento escalar (índice completo) o tensor copia (índice parcial).
  L::Value* emitTensorIndex(const Index& ix, const Type* ot) {
    L::Value* obj = emitExpr(*ix.object);
    L::Value* arr = emitIndexArray(ix);
    const size_t n = 1 + ix.rest.size();
    std::vector<L::Value*> args = {obj, cstr(ix.loc.file), B_.getInt64(static_cast<uint64_t>(ix.loc.line)),
                        B_.getInt64(n), arr};
    L::Value* res;
    if (n == ot->shape.size()) {
      res = ot->elem->kind == TypeKind::Float
                ? rc("lg_tensor_index_float", F64(), {P(), P(), I64(), I64(), P()}, args)
                : rc("lg_tensor_index_int", I64(), {P(), P(), I64(), I64(), P()}, args);
    } else {
      res = rc("lg_tensor_slice", P(), {P(), P(), I64(), I64(), P()}, args);  // posesión +1
    }
    releaseIf(obj, ot);
    return res;
  }

  L::Value* emitShortCircuit(const Binary& b) {
    bool isAnd = b.op == BinaryOp::And;
    L::Value* l = emitExpr(*b.lhs);
    L::BasicBlock* lhsBB = B_.GetInsertBlock();
    L::BasicBlock* rhsBB = newBB("sc.rhs");
    L::BasicBlock* endBB = newBB("sc.end");
    if (isAnd) B_.CreateCondBr(l, rhsBB, endBB);
    else B_.CreateCondBr(l, endBB, rhsBB);
    B_.SetInsertPoint(rhsBB);
    L::Value* r = emitExpr(*b.rhs);
    L::BasicBlock* rhsEnd = B_.GetInsertBlock();
    B_.CreateBr(endBB);
    B_.SetInsertPoint(endBB);
    L::PHINode* phi = B_.CreatePHI(I1(), 2);
    phi->addIncoming(B_.getInt1(!isAnd), lhsBB);
    phi->addIncoming(r, rhsEnd);
    return phi;
  }

  // Operadores sobre operandos prestados (el llamador libera los temporales).
  L::Value* emitBinOp(BinaryOp op, L::Value* a, L::Value* b, const Type* t, const Loc& loc) {
    if (op == BinaryOp::Eq) return emitEq(a, b, t, loc);
    if (op == BinaryOp::Ne) return B_.CreateNot(emitEq(a, b, t, loc));
    switch (t->kind) {
      case TypeKind::Int:
        switch (op) {
          case BinaryOp::Add: return checkedArith(L::Intrinsic::sadd_with_overflow, a, b, loc, "+");
          case BinaryOp::Sub: return checkedArith(L::Intrinsic::ssub_with_overflow, a, b, loc, "-");
          case BinaryOp::Mul: return checkedArith(L::Intrinsic::smul_with_overflow, a, b, loc, "*");
          case BinaryOp::Div: return checkedDiv(a, b, false, loc);
          case BinaryOp::Mod: return checkedDiv(a, b, true, loc);
          case BinaryOp::Lt: return B_.CreateICmpSLT(a, b);
          case BinaryOp::Gt: return B_.CreateICmpSGT(a, b);
          case BinaryOp::Le: return B_.CreateICmpSLE(a, b);
          case BinaryOp::Ge: return B_.CreateICmpSGE(a, b);
          default: break;
        }
        break;
      case TypeKind::Float:
        switch (op) {
          case BinaryOp::Add: return B_.CreateFAdd(a, b);
          case BinaryOp::Sub: return B_.CreateFSub(a, b);
          case BinaryOp::Mul: return B_.CreateFMul(a, b);
          case BinaryOp::Div: return B_.CreateFDiv(a, b);
          case BinaryOp::Mod: return B_.CreateFRem(a, b);
          case BinaryOp::Lt: return B_.CreateFCmpOLT(a, b);
          case BinaryOp::Gt: return B_.CreateFCmpOGT(a, b);
          case BinaryOp::Le: return B_.CreateFCmpOLE(a, b);
          case BinaryOp::Ge: return B_.CreateFCmpOGE(a, b);
          default: break;
        }
        break;
      case TypeKind::Str:
        if (op == BinaryOp::Add) return rc("lg_str_concat", P(), {P(), P()}, {a, b});
        break;
      case TypeKind::Tensor:
        switch (op) {
          case BinaryOp::MatMul: return rc("lg_tensor_matmul", P(), {P(), P()}, {a, b});
          case BinaryOp::Add: return rc("lg_tensor_add", P(), {P(), P()}, {a, b});
          case BinaryOp::Sub: return rc("lg_tensor_sub", P(), {P(), P()}, {a, b});
          case BinaryOp::Mul: return rc("lg_tensor_mul", P(), {P(), P()}, {a, b});
          default: break;
        }
        break;
      case TypeKind::Bool:
        if (op == BinaryOp::And) return B_.CreateAnd(a, b);  // no ocurre: and/or van por cortocircuito
        break;
      default: break;
    }
    fail(loc, "operador no soportado para el tipo " + t->str());
  }

  // Igualdad de valores prestados.
  L::Value* emitEq(L::Value* a, L::Value* b, const Type* t, const Loc& loc) {
    switch (t->kind) {
      case TypeKind::Int:
      case TypeKind::Bool: return B_.CreateICmpEQ(a, b);
      case TypeKind::Float: return B_.CreateFCmpOEQ(a, b);
      case TypeKind::Str:
        return B_.CreateICmpNE(rc("lg_str_eq", I64(), {P(), P()}, {a, b}), B_.getInt64(0));
      case TypeKind::Struct: {
        auto mit = methods_.find(t->name);
        if (mit != methods_.end()) {
          auto eit = mit->second.find("eq");
          if (eit != mit->second.end()) {
            L::Function* F = mod_.getFunction(fnName(*eit->second));
            if (!F) fail(loc, "método eq sin función generada");
            return B_.CreateCall(F, {a, b});
          }
        }
        return B_.CreateICmpEQ(a, b);  // sin método eq: identidad
      }
      default: break;
    }
    fail(loc, "== no soportado para el tipo " + t->str(), "compara los campos explícitamente");
  }

  // x in c: diccionario (clave) o lista (búsqueda lineal).
  L::Value* emitIn(L::Value* x, L::Value* c, const Type* xt, const Type* ct, const Loc& loc) {
    if (ct->kind == TypeKind::Dict) {
      L::Value* has = rc("lg_dict_has", I64(), {P(), I64()}, {c, toSlot(x, xt)});
      return B_.CreateICmpNE(has, B_.getInt64(0));
    }
    if (ct->kind != TypeKind::List)
      fail(loc, "'in' no soportado sobre " + ct->str(), "usa 'in' con listas o diccionarios");
    L::AllocaInst* res = createEntryAlloca(I8(), "in.res");
    L::AllocaInst* idx = createEntryAlloca(I64(), "in.idx");
    B_.CreateStore(B_.getInt8(0), res);
    B_.CreateStore(B_.getInt64(0), idx);
    L::Value* n = rc("lg_list_len", I64(), {P()}, {c});
    L::BasicBlock* header = newBB("in.cond");
    L::BasicBlock* body = newBB("in.body");
    L::BasicBlock* found = newBB("in.found");
    L::BasicBlock* next = newBB("in.next");
    L::BasicBlock* after = newBB("in.end");
    B_.CreateBr(header);
    B_.SetInsertPoint(header);
    B_.CreateCondBr(B_.CreateICmpSLT(B_.CreateLoad(I64(), idx), n), body, after);
    B_.SetInsertPoint(body);
    L::Value* slot = rc("lg_list_get", I64(), {P(), I64()}, {c, B_.CreateLoad(I64(), idx)});
    L::Value* el = fromSlot(slot, ct->elem);
    L::Value* eq = emitEq(el, x, ct->elem, loc);
    B_.CreateCondBr(eq, found, next);
    B_.SetInsertPoint(found);
    B_.CreateStore(B_.getInt8(1), res);
    B_.CreateBr(after);
    B_.SetInsertPoint(next);
    B_.CreateStore(B_.CreateAdd(B_.CreateLoad(I64(), idx), B_.getInt64(1)), idx);
    B_.CreateBr(header);
    B_.SetInsertPoint(after);
    return B_.CreateICmpNE(B_.CreateLoad(I8(), res), B_.getInt8(0));
  }

  L::Value* emitIndex(const Index& ix) {
    const Type* ot = typeOf(*ix.object);
    if (ot->kind == TypeKind::Tensor) return emitTensorIndex(ix, ot);
    const Type* it = typeOf(*ix.index);
    L::Value* obj = emitExpr(*ix.object);
    L::Value* idx = emitExpr(*ix.index);
    L::Value* res = nullptr;
    const Type* rt = nullptr;
    switch (ot->kind) {
      case TypeKind::List:
        rt = ot->elem;
        res = fromSlot(rc("lg_list_get", I64(), {P(), I64()}, {obj, idx}), rt);
        break;
      case TypeKind::Dict:
        rt = ot->val;
        res = fromSlot(rc("lg_dict_get_or_panic", I64(), {P(), I64()}, {obj, toSlot(idx, it)}), rt);
        break;
      default: fail(ix.loc, "indexación no soportada sobre " + ot->str());
    }
    retainIf(res, rt);  // lg_list_get / lg_dict_get_or_panic no retienen
    releaseIf(obj, ot);
    releaseIf(idx, it);
    return res;
  }

  // ------------------------------------------------------------------ llamadas

  // Asigna cada argumento (en orden de origen) a su posición formal, por nombre o posición.
  std::vector<size_t> argSlots(const Call& c, const std::vector<std::string>& formals) {
    if (c.args.size() != formals.size())
      fail(c.loc, "número de argumentos incorrecto: se esperaban " + std::to_string(formals.size()) +
                      ", recibidos " + std::to_string(c.args.size()));
    std::vector<size_t> out(c.args.size(), 0);
    std::vector<bool> used(formals.size(), false);
    for (size_t i = 0; i < c.args.size(); ++i) {
      std::string nm = i < c.argNames.size() ? c.argNames[i] : "";
      size_t pos = i;
      if (!nm.empty()) {
        bool found = false;
        for (size_t j = 0; j < formals.size(); ++j)
          if (formals[j] == nm) { pos = j; found = true; break; }
        if (!found) fail(c.args[i]->loc, "argumento con nombre desconocido: '" + nm + "'");
      }
      if (used[pos]) fail(c.args[i]->loc, "argumento repetido para la posición " + std::to_string(pos + 1));
      used[pos] = true;
      out[i] = pos;
    }
    return out;
  }

  L::Value* externArg(L::Value* v, const Type* t) {
    return t->kind == TypeKind::Bool ? B_.CreateZExt(v, I8()) : v;
  }

  L::Value* emitCall(const Call& c) {
    switch (c.callKind) {
      case CallKind::Function:
      case CallKind::Extern: return emitUserCall(c, c.target, nullptr);
      case CallKind::Method:
        return emitUserCall(c, c.target, static_cast<const Member*>(c.callee.get()));
      case CallKind::StructCtor: return emitStructCtor(c);
      case CallKind::Builtin: return emitBuiltin(c);
      case CallKind::BuiltinMethod: return emitBuiltinMethod(c);
      case CallKind::Closure: return emitClosureCall(c);
      case CallKind::Spawn: return emitSpawn(c);
      default: break;
    }
    fail(c.loc, "llamada sin resolver (sema)");
  }

  // ------------------------------------------------------------------ clausuras

  // Función generada para una función anidada: primer argumento = la clausura (self).
  L::Function* genClosure(const FnDecl& fd) {
    if (!fd.funcType || fd.funcType->kind != TypeKind::Func) fail(fd.loc, "función anidada sin tipo (sema)");
    std::vector<L::Type*> ps = {P()};
    for (const auto& p : fd.params) ps.push_back(ssaTy(p.type, p.loc));
    L::Type* rt = (!fd.retType || fd.retType->kind == TypeKind::Void) ? VOID() : ssaTy(fd.retType, fd.loc);
    L::Function* F = L::Function::Create(L::FunctionType::get(rt, ps, false), L::Function::InternalLinkage,
                                         fd.mangled, mod_);
    DbgLocGuard noLoc(dbg_, B_, Loc{});  // el cuerpo de la clausura no hereda la ubicación del llamador
    DbgFunctionScope dfs(dbg_, F, fd.name.empty() ? fd.mangled : fd.name, fd.mangled, &fd, fd.loc);

    // Guarda el estado del generador: se genera el cuerpo como una función independiente.
    std::vector<Scope> savedScopes = std::move(scopes_);
    std::vector<LoopCtx> savedLoops = std::move(loops_);
    scopes_.clear();
    loops_.clear();
    L::Function* savedFn = curFn_;
    L::UncondBrInst* savedAnchor = anchor_;
    const Type* savedRet = curRetType_;
    const bool savedMain = inMain_;
    L::BasicBlock* savedBB = B_.GetInsertBlock();

    L::BasicBlock* entry = L::BasicBlock::Create(ctx_, "entry", F);
    L::BasicBlock* body = L::BasicBlock::Create(ctx_, "body", F);
    anchor_ = L::UncondBrInst::Create(body, entry);
    curFn_ = F;
    curRetType_ = fd.retType;
    inMain_ = false;
    B_.SetInsertPoint(body);

    pushScope();
    L::Value* self = F->getArg(0);
    L::StructType* ct = cloTy(fd);
    // Dentro de su propio cuerpo, el nombre de la función es la propia clausura.
    if (!fd.name.empty()) {
      L::AllocaInst* a = createEntryAlloca(P(), fd.name);
      B_.CreateStore(self, a);
      VarInfo vi;
      vi.slot = a;
      vi.type = fd.funcType;
      scopes_.back().names[fd.name] = vi;
    }
    // Capturas: las inmutables son campos de la clausura; las mut, la celda que apunta un campo.
    for (size_t j = 0; j < fd.captures.size(); ++j) {
      const Capture& c = fd.captures[j];
      L::IRBuilder<> AB(ctx_);
      AB.SetInsertPoint(anchor_);
      L::Value* fp = AB.CreateStructGEP(ct, self, static_cast<unsigned>(4 + j));
      VarInfo vi;
      vi.type = c.type;
      if (c.byRef) {
        L::AllocaInst* a = createEntryAlloca(P(), c.name);
        AB.CreateStore(AB.CreateLoad(P(), fp), a);
        vi.slot = a;
        vi.cell = true;
      } else {
        vi.slot = fp;
      }
      scopes_.back().names[c.name] = vi;
    }
    for (size_t i = 0; i < fd.params.size(); ++i) {
      const Param& p = fd.params[i];
      DbgLocGuard pl(dbg_, B_, p.loc);
      declareVar(p.name, p.type, F->getArg(static_cast<unsigned>(i + 1)), false, p.captured, true);
    }
    for (const auto& s : fd.body) emitStmt(*s);
    popScopeRelease();
    scopes_.clear();
    finalizeBlocks(F);

    scopes_ = std::move(savedScopes);
    loops_ = std::move(savedLoops);
    curFn_ = savedFn;
    anchor_ = savedAnchor;
    curRetType_ = savedRet;
    inMain_ = savedMain;
    if (savedBB) B_.SetInsertPoint(savedBB);
    return F;
  }

  // Destroy de la clausura: libera las capturas (copias o referencias a celdas) y la memoria.
  L::Function* cloDestroy(const FnDecl& fd) {
    auto it = cloDestroys_.find(&fd);
    if (it != cloDestroys_.end()) return it->second;
    L::StructType* ct = cloTy(fd);
    L::Function* F = makeHelper("destroy_" + fd.mangled, [&](L::Value* obj) {
      for (size_t j = 0; j < fd.captures.size(); ++j) {
        const Capture& c = fd.captures[j];
        L::Value* fp = B_.CreateStructGEP(ct, obj, static_cast<unsigned>(4 + j));
        if (c.byRef) {
          releaseRaw(B_.CreateLoad(P(), fp));
        } else if (c.type->isHeap()) {
          releaseIf(fromMem(B_.CreateLoad(memTy(c.type), fp), c.type), c.type);
        }
      }
    });
    cloDestroys_[&fd] = F;
    return F;
  }

  // `fn` dentro de un bloque: genera la función y crea la clausura con sus capturas en este punto.
  void emitNestedFn(const FnDecl& fd) {
    L::Function* F = genClosure(fd);
    L::StructType* ct = cloTy(fd);
    L::Value* obj = rc("lg_object_new", P(), {I64(), P()}, {B_.getInt64(allocSize(ct)), cloDestroy(fd)});
    B_.CreateStore(F, B_.CreateStructGEP(ct, obj, 3));
    for (size_t j = 0; j < fd.captures.size(); ++j) {
      const Capture& c = fd.captures[j];
      VarInfo src = lookup(c.name, fd.loc);
      L::Value* fp = B_.CreateStructGEP(ct, obj, static_cast<unsigned>(4 + j));
      if (c.byRef) {
        if (!src.cell) fail(fd.loc, "captura por referencia de '" + c.name + "' sin celda (error interno)");
        L::Value* cp = B_.CreateLoad(P(), src.slot);
        rc("lg_retain", VOID(), {P()}, {cp});  // la clausura comparte la celda
        B_.CreateStore(cp, fp);
      } else {
        L::Value* v = loadVar(src);
        retainIf(v, c.type);  // la clausura guarda su propia copia
        B_.CreateStore(toMem(v, c.type), fp);
      }
    }
    declareVar(fd.name, fd.funcType, obj, true);
  }

  // f(args) con f de tipo fn(...): la función recibe la clausura como primer argumento.
  L::Value* emitClosureCall(const Call& c) {
    const Type* ft = typeOf(*c.callee);
    if (ft->kind != TypeKind::Func) fail(c.loc, "llamada a un valor que no es función: " + ft->str());
    L::Value* clo = emitExpr(*c.callee);  // poseído: se libera tras la llamada
    L::Value* fnp = B_.CreateLoad(P(), B_.CreateStructGEP(L::StructType::get(ctx_, {I64(), P(), I64(), P()}), clo, 3));
    std::vector<L::Type*> ps = {P()};
    for (const Type* p : ft->params) ps.push_back(ssaTy(p, c.loc));
    L::Type* rt = ft->ret->kind == TypeKind::Void ? VOID() : ssaTy(ft->ret, c.loc);
    L::FunctionType* fty = L::FunctionType::get(rt, ps, false);
    std::vector<L::Value*> argv = {clo};
    std::vector<std::pair<L::Value*, const Type*>> temps;
    for (const auto& a : c.args) {
      const Type* at = typeOf(*a);
      L::Value* v = emitExpr(*a);
      argv.push_back(v);
      temps.push_back({v, at});
    }
    L::Value* r = B_.CreateCall(fty, fnp, argv);
    for (auto& t : temps) releaseIf(t.first, t.second);
    releaseIf(clo, ft);
    if (ft->ret->kind == TypeKind::Void) return nullptr;
    return r;
  }

  L::Value* emitUserCall(const Call& c, const std::string& target, const Member* m) {
    auto fit = fnDecls_.find(target);
    if (fit == fnDecls_.end()) fail(c.loc, "función no declarada: " + target);
    const FnDecl* fd = fit->second;
    L::Function* F = mod_.getFunction(target);
    if (!F) fail(c.loc, "función sin generar: " + target);
    bool isExt = fd->isExtern;

    size_t first = 0;
    if (m) {
      if (fd->params.empty() || !fd->params[0].isSelf) fail(c.loc, "método sin receptor: " + target);
      first = 1;
    }
    std::vector<const Param*> formals;
    std::vector<std::string> names;
    for (size_t i = first; i < fd->params.size(); ++i) {
      formals.push_back(&fd->params[i]);
      names.push_back(fd->params[i].name);
    }
    std::vector<size_t> slot = argSlots(c, names);

    size_t off = m ? 1 : 0;
    std::vector<L::Value*> argv(off + formals.size(), nullptr);
    std::vector<std::pair<L::Value*, const Type*>> temps;  // a liberar tras la llamada
    if (m) {
      const Type* rt = typeOf(*m->object);
      L::Value* recv = emitExpr(*m->object);
      argv[0] = recv;
      temps.push_back({recv, rt});
    }
    for (size_t i = 0; i < c.args.size(); ++i) {
      const Type* at = typeOf(*c.args[i]);
      L::Value* v = emitExpr(*c.args[i]);
      size_t f = slot[i];
      checkShape(v, at, formals[f]->type, c.args[i]->loc);
      argv[off + f] = isExt ? externArg(v, at) : v;
      temps.push_back({v, at});
    }
    L::Value* r = B_.CreateCall(F, argv);
    for (auto& t : temps) releaseIf(t.first, t.second);

    if (!fd->retType || fd->retType->kind == TypeKind::Void) return nullptr;
    if (isExt && fd->retType->kind == TypeKind::Bool) return B_.CreateTrunc(r, I1());
    return r;
  }

  L::Value* emitStructCtor(const Call& c) {
    const StructDecl* sd = structDecl(c.target, c.loc);
    std::vector<std::string> names;
    for (const auto& f : sd->fields) names.push_back(f.name);
    std::vector<size_t> slot = argSlots(c, names);
    std::vector<L::Value*> vals(names.size(), nullptr);
    for (size_t i = 0; i < c.args.size(); ++i) {
      const Type* at = typeOf(*c.args[i]);
      L::Value* v = emitExpr(*c.args[i]);
      checkShape(v, at, fieldType(sd->fields[slot[i]], c.loc), c.args[i]->loc);
      vals[slot[i]] = v;  // la posesión pasa al campo
    }
    L::StructType* sty = structTy(c.target, c.loc);
    uint64_t size = mod_.getDataLayout().getTypeAllocSize(sty).getFixedValue();
    L::Value* obj = rc("lg_object_new", P(), {I64(), P()}, {B_.getInt64(size), destroyFns_[c.target]});
    for (size_t j = 0; j < sd->fields.size(); ++j) {
      const Type* ft = fieldType(sd->fields[j], c.loc);
      L::Value* fp = B_.CreateStructGEP(sty, obj, static_cast<unsigned>(j + 3));
      B_.CreateStore(toMem(vals[j], ft), fp);
    }
    return obj;
  }

  // ------------------------------------------------------------------ hilos, async y canales (v2)

  // Caja Option con etiqueta dinámica (i64: 0 = None, 1 = Some) y valor SSA (posesión transferida).
  L::Value* allocOptionDyn(const Type* ot, L::Value* tag, L::Value* v) {
    L::StructType* st = optTy(ot);
    L::Value* o = rc("lg_object_new", P(), {I64(), P()}, {B_.getInt64(allocSize(st)), optDestroy(ot)});
    B_.CreateStore(tag, B_.CreateStructGEP(st, o, 3));
    B_.CreateStore(toMem(v, ot->elem), B_.CreateStructGEP(st, o, 4));
    return o;
  }

  // await h: espera el hilo y devuelve su resultado (poseído). h se suelta aquí.
  L::Value* emitAwait(const AwaitExpr& a) {
    const Type* ht = typeOf(*a.operand);
    L::Value* h = emitExpr(*a.operand);
    L::Value* slot = rc("lg_hilo_esperar", I64(), {P()}, {h});
    releaseIf(h, ht);
    return fromSlot(slot, ht->elem);
  }

  // Envoltorio de un hilo para f: lee los argumentos de los slots, llama a f, suelta los argumentos
  // heap (que el hilo poseía) y devuelve el resultado como slot. Se genera una vez por función.
  L::Function* hiloWrapper(const FnDecl& fd) {
    auto it = hiloWraps_.find(&fd);
    if (it != hiloWraps_.end()) return it->second;
    L::Function* target = mod_.getFunction(fnName(fd));
    if (!target) fail(fd.loc, "función sin generar para hilo: " + fnName(fd));
    L::Function* W = L::Function::Create(L::FunctionType::get(I64(), {P()}, false),
                                         L::Function::InternalLinkage, "hilo_" + fnName(fd), mod_);
    hiloWraps_[&fd] = W;
    L::BasicBlock* saved = B_.GetInsertBlock();
    DbgLocGuard noLoc(dbg_, B_, Loc{});  // el envoltorio no tiene ubicación de depuración
    B_.SetInsertPoint(L::BasicBlock::Create(ctx_, "entry", W));
    std::vector<L::Value*> argv;
    for (size_t i = 0; i < fd.params.size(); ++i) {
      L::Value* sp = B_.CreateConstInBoundsGEP1_64(I64(), W->getArg(0), i);
      argv.push_back(fromSlot(B_.CreateLoad(I64(), sp), fd.params[i].type));
    }
    L::Value* r = B_.CreateCall(target, argv);
    for (size_t i = 0; i < fd.params.size(); ++i) releaseIf(argv[i], fd.params[i].type);
    B_.CreateRet(toSlot(r, fd.retType));
    if (saved) B_.SetInsertPoint(saved);
    return W;
  }

  // hilo f(args) y llamadas a async fn: los argumentos (ya poseídos) pasan al hilo nuevo.
  // Por eso no se sueltan aquí: el envoltorio los libera al terminar.
  L::Value* emitSpawn(const Call& c) {
    auto fit = fnDecls_.find(c.target);
    if (fit == fnDecls_.end()) fail(c.loc, "función no declarada: " + c.target);
    const FnDecl* fd = fit->second;
    L::Function* wrap = hiloWrapper(*fd);
    const size_t n = c.args.size();
    L::ArrayType* arrTy = L::ArrayType::get(I64(), n > 0 ? n : 1);
    L::AllocaInst* arr = createEntryAlloca(arrTy, "hilo.args");
    for (size_t i = 0; i < n; ++i) {
      const Type* pt = fd->params[i].type;
      L::Value* v = emitExpr(*c.args[i]);
      checkShape(v, typeOf(*c.args[i]), pt, c.args[i]->loc);
      B_.CreateStore(toSlot(v, pt), B_.CreateConstInBoundsGEP2_64(arrTy, arr, 0, i));
    }
    const bool heapRes = fd->retType->isHeap();
    return rc("lg_hilo_lanzar", P(), {P(), P(), I64(), I64()},
              {wrap, arr, B_.getInt64(n), B_.getInt64(heapRes ? 1 : 0)});
  }

  L::Value* emitBuiltin(const Call& c) {
    if (c.callee->kind != ExprKind::Ident) fail(c.loc, "builtin con callee no nominal");
    const std::string& n = static_cast<const Ident&>(*c.callee).name;

    if (n == "Canal") {
      const Type* ct = typeOf(c);
      return rc("lg_canal_nuevo", P(), {I64()}, {B_.getInt64(ct->elem->isHeap() ? 1 : 0)});
    }
    if (n == "debil") {
      expectArgs(c, 1, n);
      const Type* at = typeOf(*c.args[0]);
      L::Value* v = emitExpr(*c.args[0]);
      L::Value* d = rc("lg_debil_new", P(), {P()}, {v});  // no toma posesión de v
      releaseIf(v, at);
      return d;
    }

    if (n == "print") {
      for (size_t i = 0; i < c.args.size(); ++i) {
        if (i) rc("lg_print_space", VOID(), {}, {});
        const Type* t = typeOf(*c.args[i]);
        L::Value* v = emitExpr(*c.args[i]);
        emitPrint(v, t, false, c.args[i]->loc);
        releaseIf(v, t);
      }
      rc("lg_print_newline", VOID(), {}, {});
      return nullptr;
    }
    if (n == "zeros" || n == "ones" || n == "randn" || n == "from_list") return emitTensorCtor(c, n);
    if (n == "Some") {
      expectArgs(c, 1, n);
      const Type* ot = typeOf(c);
      if (ot->kind != TypeKind::Option) fail(c.loc, "Some() sin tipo Option (sema)");
      return allocOption(ot, true, emitExpr(*c.args[0]));  // transfiere la posesión a la caja
    }

    expectArgs(c, 1, n);
    const Type* t = typeOf(*c.args[0]);
    L::Value* v = emitExpr(*c.args[0]);
    L::Value* res = nullptr;
    if (n == "len") {
      switch (t->kind) {
        case TypeKind::Str: res = rc("lg_str_len", I64(), {P()}, {v}); break;
        case TypeKind::List: res = rc("lg_list_len", I64(), {P()}, {v}); break;
        case TypeKind::Dict: res = rc("lg_dict_len", I64(), {P()}, {v}); break;
        case TypeKind::Tensor: res = rc("lg_tensor_dim", I64(), {P(), I64()}, {v, B_.getInt64(0)}); break;
        default: fail(c.loc, "len() no aplicable a " + t->str());
      }
    } else if (n == "int") {
      switch (t->kind) {
        case TypeKind::Int: res = v; break;
        case TypeKind::Float: res = checkedFloatToInt(v, c.loc); break;
        case TypeKind::Bool: res = B_.CreateZExt(v, I64()); break;
        case TypeKind::Str: res = rc("lg_str_to_int", I64(), {P()}, {v}); break;
        default: fail(c.loc, "int() no aplicable a " + t->str());
      }
    } else if (n == "float") {
      switch (t->kind) {
        case TypeKind::Int: res = B_.CreateSIToFP(v, F64()); break;
        case TypeKind::Float: res = v; break;
        case TypeKind::Str: res = rc("lg_str_to_float", F64(), {P()}, {v}); break;
        default: fail(c.loc, "float() no aplicable a " + t->str());
      }
    } else if (n == "str") {
      switch (t->kind) {
        case TypeKind::Str: return v;  // ya es una referencia propia
        case TypeKind::Int: res = rc("lg_str_from_int", P(), {I64()}, {v}); break;
        case TypeKind::Float: res = rc("lg_str_from_float", P(), {F64()}, {v}); break;
        case TypeKind::Bool: res = rc("lg_str_from_bool", P(), {I64()}, {B_.CreateZExt(v, I64())}); break;
        default: fail(c.loc, "str() no aplicable a " + t->str());
      }
    } else {
      fail(c.loc, "builtin desconocido: " + n);
    }
    releaseIf(v, t);
    return res;
  }

  void expectArgs(const Call& c, size_t n, const std::string& name) {
    if (c.args.size() != n)
      fail(c.loc, name + "() espera " + std::to_string(n) + " argumento(s)");
  }

  // zeros/ones/randn(d1, d2, ...) o zeros([d1, d2]); from_list(lista, d1, ...) o from_list(lista, [..]).
  L::Value* emitTensorCtor(const Call& c, const std::string& n) {
    const Type* tt = c.type;
    if (!tt || tt->kind != TypeKind::Tensor) fail(c.loc, n + "() debe producir un Tensor");
    int64_t dtype = (tt->elem && tt->elem->kind == TypeKind::Int) ? LG_DT_INT : LG_DT_FLOAT;
    size_t first = n == "from_list" ? 1 : 0;
    if (c.args.size() <= first) fail(c.loc, n + "() necesita argumentos");
    size_t nargs = c.args.size() - first;

    L::Value* values = nullptr;
    const Type* valuesT = nullptr;
    if (n == "from_list") {
      valuesT = typeOf(*c.args[0]);
      if (valuesT->kind != TypeKind::List) fail(c.args[0]->loc, "from_list() espera una lista");
      values = emitExpr(*c.args[0]);
    }

    bool listForm = nargs == 1 && typeOf(*c.args[first])->kind == TypeKind::List;
    size_t rank = listForm ? tt->shape.size() : nargs;
    if (rank == 0) fail(c.loc, "no se puede determinar la forma (rango) del tensor");

    L::AllocaInst* arr = createEntryAlloca(L::ArrayType::get(I64(), rank), "tshape");
    L::Value* dimList = nullptr;
    const Type* dimListT = nullptr;
    if (listForm) {
      dimListT = typeOf(*c.args[first]);
      dimList = emitExpr(*c.args[first]);
      for (size_t i = 0; i < rank; ++i) {
        L::Value* d = rc("lg_list_get", I64(), {P(), I64()}, {dimList, B_.getInt64(i)});
        B_.CreateStore(d, B_.CreateInBoundsGEP(I64(), arr, {B_.getInt64(i)}));
      }
    } else {
      for (size_t i = 0; i < nargs; ++i) {
        const Type* dt = typeOf(*c.args[first + i]);
        if (dt->kind != TypeKind::Int) fail(c.args[first + i]->loc, "las dimensiones deben ser int");
        L::Value* d = emitExpr(*c.args[first + i]);
        B_.CreateStore(d, B_.CreateInBoundsGEP(I64(), arr, {B_.getInt64(i)}));
      }
    }
    L::Value* dt = B_.getInt64(static_cast<uint64_t>(dtype));
    L::Value* rk = B_.getInt64(rank);
    L::Value* res = nullptr;
    if (n == "zeros") res = rc("lg_tensor_zeros", P(), {I64(), I64(), P()}, {dt, rk, arr});
    else if (n == "ones") res = rc("lg_tensor_ones", P(), {I64(), I64(), P()}, {dt, rk, arr});
    else if (n == "randn") res = rc("lg_tensor_randn", P(), {I64(), I64(), P()}, {dt, rk, arr});
    else res = rc("lg_tensor_from_list", P(), {I64(), P(), I64(), P()}, {dt, values, rk, arr});
    releaseIf(values, valuesT);  // lista prestada al runtime
    releaseIf(dimList, dimListT);
    return res;
  }

  // Métodos builtin: lista.append/pop/len, dict.tiene/len, tensor.transpose/forma/numel, str.len.
  L::Value* emitBuiltinMethod(const Call& c) {
    if (c.callee->kind != ExprKind::Member) fail(c.loc, "método builtin sin receptor");
    const auto& m = static_cast<const Member&>(*c.callee);
    const Type* ot = typeOf(*m.object);
    L::Value* obj = emitExpr(*m.object);
    L::Value* res = nullptr;
    const std::string& n = m.name;
    if (ot->kind == TypeKind::List) {
      if (n == "append") {
        expectArgs(c, 1, n);
        L::Value* v = emitExpr(*c.args[0]);
        rc("lg_list_append", VOID(), {P(), I64()}, {obj, toSlot(v, ot->elem)});  // transfiere
      } else if (n == "pop") {
        res = fromSlot(rc("lg_list_pop", I64(), {P()}, {obj}), ot->elem);  // posesión devuelta
      } else if (n == "len") {
        res = rc("lg_list_len", I64(), {P()}, {obj});
      } else {
        fail(c.loc, "método de List desconocido: " + n);
      }
    } else if (ot->kind == TypeKind::Dict) {
      if (n == "tiene") {
        expectArgs(c, 1, n);
        const Type* kt = typeOf(*c.args[0]);
        L::Value* k = emitExpr(*c.args[0]);
        L::Value* has = rc("lg_dict_has", I64(), {P(), I64()}, {obj, toSlot(k, kt)});
        res = B_.CreateICmpNE(has, B_.getInt64(0));
        releaseIf(k, kt);
      } else if (n == "len") {
        res = rc("lg_dict_len", I64(), {P()}, {obj});
      } else {
        fail(c.loc, "método de Dict desconocido: " + n);
      }
    } else if (ot->kind == TypeKind::Tensor) {
      if (n == "transpose") res = rc("lg_tensor_transpose", P(), {P()}, {obj});
      else if (n == "forma") res = rc("lg_tensor_shape_list", P(), {P()}, {obj});
      else if (n == "numel") res = rc("lg_tensor_numel", I64(), {P()}, {obj});
      else fail(c.loc, "método de Tensor desconocido: " + n);
    } else if (ot->kind == TypeKind::Str) {
      if (n == "len") res = rc("lg_str_len", I64(), {P()}, {obj});
      else fail(c.loc, "método de str desconocido: " + n);
    } else if (ot->kind == TypeKind::Option) {
      L::Value* tag = B_.CreateLoad(I64(), B_.CreateStructGEP(optTy(ot), obj, 3));
      if (n == "es_some") {
        res = B_.CreateICmpEQ(tag, B_.getInt64(1));
      } else if (n == "es_none") {
        res = B_.CreateICmpEQ(tag, B_.getInt64(0));
      } else if (n == "desempacar_o") {
        // Some: copia del valor (retenida) y se libera el valor por defecto; None: el valor por defecto.
        const Type* et = ot->elem;
        expectArgs(c, 1, n);
        L::Value* d = emitExpr(*c.args[0]);
        L::BasicBlock* someBB = newBB("opt.some");
        L::BasicBlock* noneBB = newBB("opt.none");
        L::BasicBlock* endBB = newBB("opt.end");
        B_.CreateCondBr(B_.CreateICmpEQ(tag, B_.getInt64(1)), someBB, noneBB);
        B_.SetInsertPoint(someBB);
        L::Value* x = fromMem(B_.CreateLoad(memTy(et), B_.CreateStructGEP(optTy(ot), obj, 4)), et);
        retainIf(x, et);
        releaseIf(d, et);
        B_.CreateBr(endBB);
        B_.SetInsertPoint(noneBB);
        B_.CreateBr(endBB);
        B_.SetInsertPoint(endBB);
        L::PHINode* phi = B_.CreatePHI(ssaTy(et, c.loc), 2);
        phi->addIncoming(x, someBB);
        phi->addIncoming(d, noneBB);
        res = phi;
      } else {
        fail(c.loc, "método de Option desconocido: " + n);
      }
    } else if (ot->kind == TypeKind::Hilo) {
      if (n == "esperar") res = fromSlot(rc("lg_hilo_esperar", I64(), {P()}, {obj}), ot->elem);
      else fail(c.loc, "método de Hilo desconocido: " + n);
    } else if (ot->kind == TypeKind::Canal) {
      if (n == "enviar") {
        expectArgs(c, 1, n);
        L::Value* v = emitExpr(*c.args[0]);
        // Transfiere la posesión de v al canal.
        rc("lg_canal_enviar", VOID(), {P(), I64(), P(), I64()},
           {obj, toSlot(v, ot->elem), cstr(c.loc.file), B_.getInt64(static_cast<uint64_t>(c.loc.line))});
      } else if (n == "recibir") {
        L::AllocaInst* ok = createEntryAlloca(I64(), "canal.ok");
        L::Value* slot = rc("lg_canal_recibir", I64(), {P(), P()}, {obj, ok});
        res = allocOptionDyn(typeOf(c), B_.CreateLoad(I64(), ok), fromSlot(slot, ot->elem));
      } else if (n == "cerrar") {
        rc("lg_canal_cerrar", VOID(), {P()}, {obj});
      } else {
        fail(c.loc, "método de Canal desconocido: " + n);
      }
    } else if (ot->kind == TypeKind::Debil) {
      if (n != "fuerte") fail(c.loc, "método de Debil desconocido: " + n);
      // lg_debil_fuerte devuelve +1 si el objeto vive, o NULL: el valor del Option es ese puntero.
      L::Value* p = rc("lg_debil_fuerte", P(), {P()}, {obj});
      res = allocOptionDyn(typeOf(c), B_.CreateZExt(B_.CreateIsNotNull(p), I64()), p);
    } else {
      fail(c.loc, "método builtin no soportado sobre " + ot->str());
    }
    releaseIf(obj, ot);
    return res;
  }

  // ------------------------------------------------------------------ impresión

  void printText(const std::string& s) { rc("lg_print_text", VOID(), {P()}, {cstr(s)}); }

  // Bucle contado: body(i) para i en [0, n).
  void forN(L::Value* n, const std::function<void(L::Value*)>& body) {
    L::AllocaInst* idx = createEntryAlloca(I64(), "forn.i");
    B_.CreateStore(B_.getInt64(0), idx);
    L::BasicBlock* header = newBB("forn.cond");
    L::BasicBlock* bodyBB = newBB("forn.body");
    L::BasicBlock* after = newBB("forn.end");
    B_.CreateBr(header);
    B_.SetInsertPoint(header);
    L::Value* i = B_.CreateLoad(I64(), idx);
    B_.CreateCondBr(B_.CreateICmpSLT(i, n), bodyBB, after);
    B_.SetInsertPoint(bodyBB);
    body(i);
    B_.CreateStore(B_.CreateAdd(B_.CreateLoad(I64(), idx), B_.getInt64(1)), idx);
    B_.CreateBr(header);
    B_.SetInsertPoint(after);
  }

  void emitIfCond(L::Value* c, const std::function<void()>& then) {
    L::BasicBlock* t = newBB("pr.then");
    L::BasicBlock* m = newBB("pr.end");
    B_.CreateCondBr(c, t, m);
    B_.SetInsertPoint(t);
    then();
    B_.CreateBr(m);
    B_.SetInsertPoint(m);
  }

  // Imprime un valor prestado. nested: strings entre comillas (dentro de contenedores y structs).
  void emitPrint(L::Value* v, const Type* t, bool nested, const Loc& loc) {
    switch (t->kind) {
      case TypeKind::Int: rc("lg_print_int", VOID(), {I64()}, {v}); return;
      case TypeKind::Float: rc("lg_print_float", VOID(), {F64()}, {v}); return;
      case TypeKind::Bool: rc("lg_print_bool", VOID(), {I64()}, {B_.CreateZExt(v, I64())}); return;
      case TypeKind::Str:
        if (nested) printText("\"");
        rc("lg_print_str", VOID(), {P()}, {v});
        if (nested) printText("\"");
        return;
      case TypeKind::Tensor: rc("lg_tensor_print", VOID(), {P()}, {v}); return;
      case TypeKind::List: {
        printText("[");
        L::Value* n = rc("lg_list_len", I64(), {P()}, {v});
        forN(n, [&](L::Value* i) {
          emitIfCond(B_.CreateICmpNE(i, B_.getInt64(0)), [&] { printText(", "); });
          L::Value* slot = rc("lg_list_get", I64(), {P(), I64()}, {v, i});
          emitPrint(fromSlot(slot, t->elem), t->elem, true, loc);
        });
        printText("]");
        return;
      }
      case TypeKind::Dict: {
        printText("{");
        L::Value* n = rc("lg_dict_len", I64(), {P()}, {v});
        forN(n, [&](L::Value* i) {
          emitIfCond(B_.CreateICmpNE(i, B_.getInt64(0)), [&] { printText(", "); });
          L::Value* k = rc("lg_dict_key_at", I64(), {P(), I64()}, {v, i});
          emitPrint(fromSlot(k, t->key), t->key, true, loc);
          printText(": ");
          L::Value* val = rc("lg_dict_val_at", I64(), {P(), I64()}, {v, i});
          emitPrint(fromSlot(val, t->val), t->val, true, loc);
        });
        printText("}");
        return;
      }
      case TypeKind::Option: {
        L::BasicBlock* someBB = newBB("pr.some");
        L::BasicBlock* noneBB = newBB("pr.none");
        L::BasicBlock* endBB = newBB("pr.optend");
        L::Value* tag = B_.CreateLoad(I64(), B_.CreateStructGEP(optTy(t), v, 3));
        B_.CreateCondBr(B_.CreateICmpEQ(tag, B_.getInt64(1)), someBB, noneBB);
        B_.SetInsertPoint(someBB);
        printText("Some(");
        emitPrint(fromMem(B_.CreateLoad(memTy(t->elem), B_.CreateStructGEP(optTy(t), v, 4)), t->elem), t->elem, true, loc);
        printText(")");
        B_.CreateBr(endBB);
        B_.SetInsertPoint(noneBB);
        printText("None");
        B_.CreateBr(endBB);
        B_.SetInsertPoint(endBB);
        return;
      }
      case TypeKind::Struct: {
        const StructDecl* sd = structDecl(t->name, loc);
        L::StructType* sty = structTy(t->name, loc);
        printText(t->name + "(");
        for (size_t j = 0; j < sd->fields.size(); ++j) {
          if (j) printText(", ");
          printText(sd->fields[j].name + ": ");
          const Type* ft = fieldType(sd->fields[j], loc);
          L::Value* fp = B_.CreateStructGEP(sty, v, static_cast<unsigned>(j + 3));
          emitPrint(fromMem(B_.CreateLoad(memTy(ft), fp), ft), ft, true, loc);
        }
        printText(")");
        return;
      }
      default: break;
    }
    fail(loc, "no se puede imprimir un valor de tipo " + t->str());
  }

  // ------------------------------------------------------------------ expresiones (despacho)
  // (emitExpr delega aquí; mantenido junto a las llamadas)
};

// Ajuste de helpers que dependen de la clase: ver cstrRaw / cstr.

}  // namespace

// ---------------------------------------------------------------------- API pública

namespace {

L::CodeGenOptLevel toLevel(int optLevel) {
  switch (optLevel) {
    case 0: return L::CodeGenOptLevel::None;
    case 1: return L::CodeGenOptLevel::Less;
    case 3: return L::CodeGenOptLevel::Aggressive;
    default: return L::CodeGenOptLevel::Default;
  }
}

std::unique_ptr<L::TargetMachine> makeHostTM(int optLevel, std::string& triple, std::string& err) {
  L::InitializeNativeTarget();
  L::InitializeNativeTargetAsmPrinter();
  triple = L::sys::getDefaultTargetTriple();
  const L::Target* target = L::TargetRegistry::lookupTarget(L::Triple(triple), err);
  if (!target) return nullptr;
  L::TargetOptions opts;
  return std::unique_ptr<L::TargetMachine>(target->createTargetMachine(
      L::Triple(triple), L::sys::getHostCPUName(), "", opts, L::Reloc::PIC_, std::nullopt, toLevel(optLevel)));
}

// Triple de --target wasm32: WASI preview 1 (wasi-libc), el que enlaza lgc con wasm-ld.
const char* const kWasmTriple = "wasm32-wasip1";

std::unique_ptr<L::TargetMachine> makeWasmTM(int optLevel, std::string& triple, std::string& err) {
  LLVMInitializeWebAssemblyTargetInfo();
  LLVMInitializeWebAssemblyTarget();
  LLVMInitializeWebAssemblyTargetMC();
  LLVMInitializeWebAssemblyAsmPrinter();
  triple = kWasmTriple;
  const L::Target* target = L::TargetRegistry::lookupTarget(L::Triple(triple), err);
  if (!target) return nullptr;
  L::TargetOptions opts;
  return std::unique_ptr<L::TargetMachine>(target->createTargetMachine(
      L::Triple(triple), "generic", "", opts, std::nullopt, std::nullopt, toLevel(optLevel)));
}

// Con --target wasm32 no hay hilos (pthread), canales (sobre hilos) ni sockets (tcp): el código
// generado llamaría a símbolos que el runtime de wasm no define. Se rechaza aquí, con un mensaje
// claro, en vez de fallar al enlazar. Solo cuentan las declaraciones que se usan de verdad.
bool checkWasmSupported(const L::Module& mod, const Program& prog, Diagnostics& diags) {
  bool ok = true;
  for (const L::Function& F : mod) {
    if (!F.isDeclaration() || F.use_empty()) continue;
    L::StringRef name = F.getName();
    if (name.starts_with("lg_hilo_") || name.starts_with("lg_canal_") || name.starts_with("lg_std_tcp_")) {
      diags.error(Loc{prog.file, 1, 1},
                  "--target wasm32: el programa usa '" + name.str() +
                      "', que no está disponible en wasm32 (hilos, canales y tcp necesitan pthread o sockets)");
      ok = false;
    }
  }
  return ok;
}

// Genera el módulo completo. Devuelve false (con diagnósticos) si falla.
bool buildModule(const Program& prog, const TypeTable& types, Diagnostics& diags, L::LLVMContext& ctx,
                 L::Module& mod, bool debug, bool wasm, const std::vector<std::string>& exports) {
  try {
    std::unique_ptr<DebugInfo> dbg;
    if (debug) dbg = std::make_unique<DebugInfo>(mod, prog);
    CodeGen cg(prog, types, diags, ctx, mod);
    cg.setDebug(dbg.get());
    cg.setExports(exports);
    if (dbg) dbg->setOptionBox([&cg](const Type* t) { return cg.optionBoxType(t); });
    cg.run();
    if (dbg) dbg->finalize();
  } catch (CgAbort&) {
    return false;
  }
  if (wasm && !checkWasmSupported(mod, prog, diags)) return false;
  std::string msg;
  L::raw_string_ostream os(msg);
  if (L::verifyModule(mod, &os)) {
    os.flush();
    diags.error(Loc{prog.file, 1, 1}, "IR inválido (error interno de codegen)", msg);
    return false;
  }
  return true;
}

}  // namespace

bool emitObjectFile(const Program& prog, const TypeTable& types, const std::string& objPath,
                    int optLevel, Diagnostics& diags, bool debug, bool wasm,
                    const std::vector<std::string>& exports) {
  std::string triple, err;
  auto tm = wasm ? makeWasmTM(optLevel, triple, err) : makeHostTM(optLevel, triple, err);
  if (!tm) {
    diags.error(Loc{prog.file, 1, 1}, std::string("no hay backend para ") + (wasm ? "wasm32" : "el host") + ": " + err);
    return false;
  }
  L::LLVMContext ctx;
  L::Module mod(prog.file, ctx);
  mod.setSourceFileName(prog.file);
  mod.setTargetTriple(L::Triple(triple));
  mod.setDataLayout(tm->createDataLayout());
  if (!buildModule(prog, types, diags, ctx, mod, debug, wasm, exports)) return false;

  if (optLevel > 0) {
    L::PassBuilder PB(tm.get());
    L::LoopAnalysisManager LAM;
    L::FunctionAnalysisManager FAM;
    L::CGSCCAnalysisManager CGAM;
    L::ModuleAnalysisManager MAM;
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
    L::OptimizationLevel lvl = optLevel == 1   ? L::OptimizationLevel::O1
                               : optLevel == 2 ? L::OptimizationLevel::O2
                                               : L::OptimizationLevel::O3;
    L::ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(lvl);
    MPM.run(mod, MAM);
  }

  std::error_code ec;
  L::raw_fd_ostream out(objPath, ec, L::sys::fs::OF_None);
  if (ec) {
    diags.error(Loc{prog.file, 1, 1}, "no se puede escribir el objeto " + objPath, ec.message());
    return false;
  }
  L::legacy::PassManager pm;
  if (tm->addPassesToEmitFile(pm, out, nullptr, L::CodeGenFileType::ObjectFile)) {
    diags.error(Loc{prog.file, 1, 1}, "el backend no puede emitir objetos para el host");
    return false;
  }
  pm.run(mod);
  out.flush();
  return true;
}

bool emitLLVMIR(const Program& prog, const TypeTable& types, std::string& irOut, Diagnostics& diags,
                bool debug, bool wasm, const std::vector<std::string>& exports) {
  std::string triple, err;
  auto tm = wasm ? makeWasmTM(0, triple, err) : makeHostTM(0, triple, err);
  if (!tm) {
    diags.error(Loc{prog.file, 1, 1}, std::string("no hay backend para ") + (wasm ? "wasm32" : "el host") + ": " + err);
    return false;
  }
  L::LLVMContext ctx;
  L::Module mod(prog.file, ctx);
  mod.setSourceFileName(prog.file);
  mod.setTargetTriple(L::Triple(triple));
  mod.setDataLayout(tm->createDataLayout());
  if (!buildModule(prog, types, diags, ctx, mod, debug, wasm, exports)) return false;
  irOut.clear();
  L::raw_string_ostream os(irOut);
  mod.print(os, nullptr);
  os.flush();
  return true;
}

}  // namespace lg
