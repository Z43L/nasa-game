// Análisis semántico (sema).
//
// Builtins (CallKind::Builtin, target = nombre):
//   print(...)                            cualquier número de argumentos -> void
//   len(List | Dict | str | Tensor)       -> int
//   int(float | int | bool | str)         -> int
//   float(int | float | str)              -> float
//   str(cualquiera)                       -> str
//   zeros(d...), ones(d...), randn(d...)  -> Tensor[float, forma]
//                                            dimensiones int; literal -> valor, si no -1
//   from_list(List[int|float], d...)      -> Tensor[int|float, forma]
//                                            con lista literal se comprueba el número de elementos
//
// Métodos builtin (CallKind::BuiltinMethod, target = "Tipo.método"):
//   List.append(x)      -> void    (lista mutable; x compatible con el elemento)
//   List.pop()          -> T       (lista mutable)
//   List.len()          -> int
//   Dict.tiene(k)       -> bool
//   Dict.len()          -> int
//   Tensor.transpose()  -> Tensor  (rango 2; forma invertida)
//   Tensor.forma()      -> List[int]
//   Tensor.numel()      -> int
//
// Option[T] (Some/None y métodos):
//   Some(x)                    -> Option[T]; con contexto Option[T], x se comprueba contra T
//   None                       -> solo con contexto Option[T]; sin él es error
//   opt == None, opt != None   -> bool (no se comparan dos Option entre sí)
//   opt.es_some(), opt.es_none() -> bool        (BuiltinMethod "Option.es_some", "Option.es_none")
//   opt.desempacar_o(v)        -> T, v de tipo T (BuiltinMethod "Option.desempacar_o")
//
// Funciones anidadas (sentencia `fn` dentro de un bloque): una variable inmutable de tipo
// fn(...) en su ámbito. Sus variables externas se registran en FnDecl::captures: una `mut` se
// captura por referencia y marca VarDecl/Param::captured. Llamar a un valor de tipo función es
// CallKind::Closure. Usar una variable de un bloque ya terminado da un error específico.
//
// Métodos de struct (CallKind::Method, target = mangled "u_Struct_metodo").
// Llamadas a struct (CallKind::StructCtor, target = nombre de la struct): solo con argumentos con nombre.
//
// Mutabilidad: asignar a un campo o a un elemento, o llamar a un método `mut self` / a
// List.append / List.pop, exige que la variable base sea `mut` (o `self` con `mut self`).
//
// Concurrencia (v2, ver docs/referencia/hilos.md):
//   hilo f(args)            -> Hilo[T]; f de nivel superior, no extern, con retorno (CallKind::Spawn)
//   async fn f(...) -> T    -> llamar f(args) devuelve Hilo[T] (CallKind::Spawn, target = f)
//   await h                 -> T, h de tipo Hilo[T] (AwaitExpr)
//   Hilo[T].esperar()       -> T
//   Canal[T]()              -> Canal[T] (builtin "Canal" con typeArgs)
//   c.enviar(x) -> void, c.recibir() -> Option[T] (None cuando está cerrado y vacío), c.cerrar() -> void
//   debil(x) -> Debil[T], x heap;  d.fuerte() -> Option[T] (Some si el objeto aún vive)
//
// Variables de nivel superior (F0): una función solo ve las `mut` de nivel superior, que son globales
// del programa (leer y asignar). Las inmutables de nivel superior no son visibles desde funciones.
// Una función lanzada con `hilo` o `async` no puede usar globales mut, ni directa ni a través de las
// funciones que llama (ver checkSpawnSafety; las llamadas por valor fn(...) no se siguen).
#include "sema/sema.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace lg {
namespace {

enum class Place { Ok, Immutable, NotPlace };

struct VarInfo {
  const Type* type = nullptr;
  bool isMut = false;
  bool isParam = false;
  bool* captured = nullptr;   // campo "captured" del VarDecl/Param que la declara (solo si es mut)
  bool* global = nullptr;     // campo "isGlobal" del VarDecl de nivel superior (solo si es mut)
  std::string file;           // archivo donde se declara (las globales solo son visibles en él)
  FnDecl* selfOf = nullptr;   // si es el nombre de una función anidada: su declaración
};

// Lanzamiento de hilo (hilo f(...) o llamada a async fn) para comprobar después del análisis.
struct SpawnSite {
  const FnDecl* target = nullptr;
  Loc loc;
};

// Función cuyo cuerpo se está analizando. base = índice del primer ámbito que le pertenece.
struct Frame {
  FnDecl* fd = nullptr;
  size_t base = 0;
};

struct StructInfo {
  StructDecl* decl = nullptr;
  const Type* type = nullptr;
  std::map<std::string, FnDecl*> methods;
};

bool isBuiltinName(const std::string& n) {
  return n == "print" || n == "len" || n == "int" || n == "float" || n == "str" ||
         n == "zeros" || n == "ones" || n == "randn" || n == "from_list" || n == "Some" ||
         n == "debil" || n == "Canal";
}

bool isReservedName(const std::string& n) {
  return isBuiltinName(n) || n == "bool" || n == "None" || n == "Option" || n == "Hilo" || n == "Debil";
}

int fieldIndex(const StructDecl* sd, const std::string& name) {
  if (!sd) return -1;
  for (size_t i = 0; i < sd->fields.size(); ++i) {
    if (sd->fields[i].name == name) return static_cast<int>(i);
  }
  return -1;
}

std::string joinFields(const StructDecl* sd) {
  std::string s;
  if (!sd) return s;
  for (size_t i = 0; i < sd->fields.size(); ++i) {
    if (i > 0) s += ", ";
    s += sd->fields[i].name;
  }
  return s;
}

bool stmtReturns(const Stmt& s);

bool blockReturns(const std::vector<StmtPtr>& body) {
  for (const auto& s : body) {
    if (s && stmtReturns(*s)) return true;
  }
  return false;
}

// Un statement "siempre devuelve" si es un return, o un if con ambas ramas que devuelven.
bool stmtReturns(const Stmt& s) {
  if (s.kind == StmtKind::Return) return true;
  if (s.kind == StmtKind::If) {
    const auto& i = static_cast<const IfStmt&>(s);
    return !i.elseBody.empty() && blockReturns(i.thenBody) && blockReturns(i.elseBody);
  }
  return false;
}

// Forma resultante de combinar dos formas compatibles: gana la dimensión conocida.
std::vector<int64_t> mergeShape(const std::vector<int64_t>& a, const std::vector<int64_t>& b) {
  std::vector<int64_t> r(a.size());
  for (size_t i = 0; i < a.size(); ++i) r[i] = a[i] >= 0 ? a[i] : b[i];
  return r;
}

class Sema {
public:
  Sema(Program& prog, TypeTable& types, Diagnostics& diags)
      : prog_(prog), types_(types), diags_(diags) {}

  void run() {
    collectStructs();
    collectSignatures();
    // Ámbito global (nivel superior). Las funciones no lo ven, pero sí se usa para avisar.
    scopes_.clear();
    scopes_.emplace_back();
    fnBase_ = 0;
    for (auto& s : prog_.stmts) {
      if (s) checkStmt(*s);
    }
    for (auto& d : prog_.decls) {
      if (d->kind == DeclKind::Fn) {
        checkBody(*static_cast<FnDecl*>(d.get()));
      } else if (d->kind == DeclKind::Impl) {
        for (auto& m : static_cast<ImplDecl*>(d.get())->methods) checkBody(*m);
      }
    }
    checkSpawnSafety();
  }

private:
  Program& prog_;
  TypeTable& types_;
  Diagnostics& diags_;

  std::map<std::string, FnDecl*> funcs_;
  std::map<std::string, StructInfo> structs_;

  std::vector<std::map<std::string, VarInfo>> scopes_;
  size_t fnBase_ = 0;               // primer ámbito propio de la función actual (0 en el nivel superior)
  const Type* curRet_ = nullptr;    // tipo de retorno de la función actual
  bool inFn_ = false;
  int loopDepth_ = 0;
  std::vector<Frame> frames_;       // funciones abiertas (de fuera a dentro)
  std::set<std::string> endedNames_;  // nombres declarados en bloques que ya terminaron (diagnóstico)
  int closureCount_ = 0;            // para nombres LLVM únicos de funciones anidadas
  // Globales mut (F0): primera global usada directamente por cada función, y aristas de llamada
  // directa (función -> función llamada). Las llamadas `hilo`/async no añaden arista: son otro hilo.
  std::map<const FnDecl*, std::string> globalUse_;
  std::map<const FnDecl*, std::vector<const FnDecl*>> callees_;
  std::vector<SpawnSite> spawns_;

  // ------------------------------------------------------------ utilidades

  void error(const Loc& loc, std::string msg, std::string hint = "") {
    diags_.error(loc, std::move(msg), std::move(hint));
  }

  const Type* err() const { return types_.error(); }

  static bool bad(const Type* t) { return !t || t->kind == TypeKind::Error; }

  bool isVoid(const Type* t) const { return t && t->kind == TypeKind::Void; }

  bool isTensor(const Type* t) const { return t && t->kind == TypeKind::Tensor; }

  bool isNumMix(const Type* l, const Type* r) const {
    return (l == types_.intT() && r == types_.floatT()) || (l == types_.floatT() && r == types_.intT());
  }

  std::string conversionHint(const Type* expected, const Type* actual) const {
    if (expected == types_.floatT() && actual == types_.intT()) return "usa float(x) para convertir el int";
    if (expected == types_.intT() && actual == types_.floatT()) return "usa int(x) para convertir el float";
    if (expected == types_.strT()) return "usa str(x) para convertir el valor a texto";
    if (expected == types_.intT() && actual == types_.strT()) return "usa int(x) para convertir el texto";
    if (expected->kind == TypeKind::Option && expected->elem == actual) return "envuelve el valor con Some(x)";
    return "";
  }

  bool hasMethod(const std::string& sname, const std::string& m) const {
    auto it = structs_.find(sname);
    return it != structs_.end() && it->second.methods.count(m) > 0;
  }

  // Busca una variable visible desde el ámbito actual. Si pertenece a una función externa, la
  // registra como captura de las funciones anidadas que la usan. Las variables globales
  // (ámbito 0) no son visibles dentro de una función.
  VarInfo* findVisible(const std::string& name) {
    for (size_t i = scopes_.size(); i-- > 0;) {
      auto it = scopes_[i].find(name);
      if (it == scopes_[i].end()) continue;
      if (i >= fnBase_) return &it->second;
      if (i == 0) {
        // Global del programa: solo las `mut` son visibles desde una función.
        // Solo las `mut` declaradas en el mismo archivo que la función (cada módulo tiene sus globales).
        if (!it->second.isMut || frames_.empty() || it->second.file != frames_.back().fd->loc.file)
          return nullptr;
        if (it->second.global) *it->second.global = true;
        noteGlobalUse(name);
        return &it->second;
      }
      recordCapture(name, it->second, i);
      return &it->second;
    }
    return nullptr;
  }

  // La función actual usa la global `name`: queda registrado para la comprobación de hilos.
  void noteGlobalUse(const std::string& name) {
    if (!frames_.empty()) globalUse_.emplace(frames_.back().fd, name);
  }

  // La función actual llama directamente a `callee` (función de nivel superior, método o anidada).
  void noteCall(const FnDecl* callee) {
    if (!frames_.empty() && callee) callees_[frames_.back().fd].push_back(callee);
  }

  static std::string fnDisplayName(const FnDecl* fd) {
    return fd->implOf.empty() ? fd->name : fd->implOf + "." + fd->name;
  }

  // Ningún `hilo`/async puede alcanzar una global mut: busca por el grafo de llamadas directas.
  void checkSpawnSafety() {
    for (const SpawnSite& sp : spawns_) {
      std::map<const FnDecl*, const FnDecl*> parent;
      std::set<const FnDecl*> seen{sp.target};
      std::vector<const FnDecl*> pending{sp.target};
      const FnDecl* hit = nullptr;
      while (!pending.empty() && !hit) {
        const FnDecl* cur = pending.back();
        pending.pop_back();
        if (globalUse_.count(cur)) {
          hit = cur;
          break;
        }
        auto cit = callees_.find(cur);
        if (cit == callees_.end()) continue;
        for (const FnDecl* next : cit->second) {
          if (!seen.insert(next).second) continue;
          parent[next] = cur;
          pending.push_back(next);
        }
      }
      if (!hit) continue;
      const std::string gname = globalUse_.at(hit);
      std::string hint = "pasa el valor como parámetro a '" + fnDisplayName(sp.target) +
                         "' y devuelve el resultado, en lugar de usar la global";
      if (hit != sp.target) {
        std::vector<std::string> chain;
        for (const FnDecl* x = hit; x != sp.target; x = parent.at(x)) chain.push_back(fnDisplayName(x));
        chain.push_back(fnDisplayName(sp.target));
        std::string path;
        for (size_t k = chain.size(); k-- > 0;) path += (path.empty() ? "" : " -> ") + chain[k];
        hint = "la cadena de llamadas " + path + " llega a '" + gname + "'; " + hint;
      }
      error(sp.loc,
            "la función '" + fnDisplayName(sp.target) + "' lanzada con hilo no puede usar la variable global mutable '" +
                gname + "': las globales mutables no son seguras entre hilos",
            hint);
    }
  }

  // La variable del ámbito `scopeIdx` (fuera de la función actual) la usa la función actual:
  // cada función anidada más interna que el ámbito la captura. Una `mut` se captura por referencia.
  void recordCapture(const std::string& name, VarInfo& vi, size_t scopeIdx) {
    for (size_t k = frames_.size(); k-- > 0;) {
      Frame& f = frames_[k];
      if (f.base <= scopeIdx) break;                       // la variable es de esta función o de una externa
      if (vi.selfOf && f.fd == vi.selfOf) break;           // la propia función: se llama a sí misma por `self`
      bool added = false;
      bool found = false;
      for (const Capture& c : f.fd->captures) {
        if (c.name == name) found = true;
      }
      if (!found) {
        Capture c;
        c.name = name;
        c.type = vi.type;
        c.byRef = vi.isMut;
        f.fd->captures.push_back(c);
        added = true;
      }
      if (added && vi.isMut && vi.captured) *vi.captured = true;
    }
  }

  // Declara una variable en el ámbito actual. Devuelve false si el nombre no es válido.
  bool declare(const std::string& name, const Type* t, bool isMut, const Loc& loc, bool isParam = false) {
    VarInfo info;
    info.type = t;
    info.isMut = isMut;
    info.isParam = isParam;
    return declareInfo(name, info, loc);
  }

  bool declareInfo(const std::string& name, const VarInfo& info, const Loc& loc) {
    if (isReservedName(name) || funcs_.count(name) || structs_.count(name)) {
      error(loc, "'" + name + "' es un nombre reservado", "elige otro nombre para la variable");
      return false;
    }
    auto& sc = scopes_.back();
    if (sc.count(name)) {
      error(loc, "'" + name + "' ya está declarada en este ámbito", "usa otro nombre o reasigna con '" + name + " = ...'");
    }
    sc[name] = info;
    sc[name].file = loc.file;
    return true;
  }

  // Cierra el ámbito de un bloque: sus nombres pasan a "ámbito terminado" para el diagnóstico.
  void popBlockScope() {
    for (const auto& kv : scopes_.back()) endedNames_.insert(kv.first);
    scopes_.pop_back();
  }

  // Comprueba que `actual` puede usarse donde se espera `expected`. Informa del error si no.
  bool checkAssignable(const Type* expected, const Type* actual, const Loc& loc) {
    if (bad(expected) || bad(actual)) return false;
    if (isVoid(actual)) {
      error(loc, "no se puede usar un valor void", "la función no devuelve valor");
      return false;
    }
    if (TypeTable::compatible(expected, actual)) return true;
    if (isTensor(expected) && isTensor(actual) && expected->elem == actual->elem &&
        expected->shape.size() == actual->shape.size()) {
      error(loc, "forma de tensor incompatible: se esperaba " + expected->str() + ", recibido " + actual->str(),
            "las dimensiones conocidas deben coincidir");
    } else {
      error(loc, "tipo esperado " + expected->str() + ", recibido " + actual->str(),
            conversionHint(expected, actual));
    }
    return false;
  }

  void checkCond(Expr& cond, const std::string& what) {
    const Type* t = checkExpr(cond, types_.boolT());
    if (!bad(t) && t != types_.boolT()) {
      error(cond.loc, what + " debe ser bool, recibido " + t->str(), "usa una comparación, por ejemplo x > 0");
    }
  }

  // ------------------------------------------------------------ tipos escritos

  const Type* resolveType(const TypeExpr& te) {
    switch (te.kind) {
      case TypeExpr::Kind::Name: {
        if (te.name == "int") return types_.intT();
        if (te.name == "float") return types_.floatT();
        if (te.name == "bool") return types_.boolT();
        if (te.name == "str") return types_.strT();
        auto it = structs_.find(te.name);
        if (it != structs_.end()) return it->second.type;
        error(te.loc, "tipo desconocido '" + te.name + "'",
              "tipos válidos: int, float, bool, str, una struct, List[T], Dict[K, V] o Tensor[float, (...)]");
        return err();
      }
      case TypeExpr::Kind::List: {
        if (te.args.size() != 1 || !te.args[0]) {
          error(te.loc, "List necesita exactamente un tipo de elemento", "escribe List[int]");
          return err();
        }
        return types_.listOf(resolveType(*te.args[0]));
      }
      case TypeExpr::Kind::Dict: {
        if (te.args.size() != 2 || !te.args[0] || !te.args[1]) {
          error(te.loc, "Dict necesita una clave y un valor", "escribe Dict[str, int]");
          return err();
        }
        const Type* k = resolveType(*te.args[0]);
        const Type* v = resolveType(*te.args[1]);
        if (bad(k) || bad(v)) return err();
        if (k != types_.intT() && k != types_.strT() && k != types_.boolT()) {
          error(te.args[0]->loc, "clave de Dict no válida: " + k->str(), "las claves pueden ser int, str o bool");
          return err();
        }
        return types_.dictOf(k, v);
      }
      case TypeExpr::Kind::Tensor: {
        if (te.args.size() != 1 || !te.args[0]) {
          error(te.loc, "Tensor necesita el tipo de elemento y la forma", "escribe Tensor[float, (2, 3)]");
          return err();
        }
        const Type* e = resolveType(*te.args[0]);
        if (bad(e)) return err();
        if (e != types_.intT() && e != types_.floatT()) {
          error(te.args[0]->loc, "el elemento de Tensor debe ser float o int, recibido " + e->str(),
                "escribe Tensor[float, (2, 3)]");
          return err();
        }
        if (te.dims.empty()) {
          error(te.loc, "falta la forma del tensor", "escribe Tensor[float, (2, 3)]");
          return err();
        }
        std::vector<int64_t> shape;
        for (int64_t d : te.dims) {
          if (d < 0) {
            error(te.loc, "dimensión inválida en el tipo de tensor: " + std::to_string(d));
            return err();
          }
          shape.push_back(d);
        }
        return types_.tensorOf(e, shape);
      }
      case TypeExpr::Kind::Option: {
        if (te.args.size() != 1 || !te.args[0]) {
          error(te.loc, "Option necesita exactamente un tipo", "escribe Option[int]");
          return err();
        }
        const Type* e = resolveType(*te.args[0]);
        if (bad(e)) return err();
        return types_.optionOf(e);
      }
      case TypeExpr::Kind::Hilo:
      case TypeExpr::Kind::Canal:
      case TypeExpr::Kind::Debil: {
        const char* nombre = te.kind == TypeExpr::Kind::Hilo ? "Hilo" : te.kind == TypeExpr::Kind::Canal ? "Canal" : "Debil";
        if (te.args.size() != 1 || !te.args[0]) {
          error(te.loc, std::string(nombre) + " necesita exactamente un tipo", std::string("escribe ") + nombre + "[int]");
          return err();
        }
        const Type* e = resolveType(*te.args[0]);
        if (bad(e)) return err();
        if (te.kind == TypeExpr::Kind::Hilo) return types_.hiloOf(e);
        if (te.kind == TypeExpr::Kind::Canal) return types_.canalOf(e);
        return types_.debilOf(e);
      }
      case TypeExpr::Kind::Func: {
        std::vector<const Type*> ps;
        bool ok = true;
        for (const auto& a : te.args) {
          if (!a) {
            ok = false;
            continue;
          }
          const Type* t = resolveType(*a);
          if (bad(t)) ok = false;
          ps.push_back(t);
        }
        const Type* r = te.ret ? resolveType(*te.ret) : types_.voidT();
        if (!ok || bad(r)) return err();
        const Type* ft = types_.funcOf(ps, r);
        if (bad(ft)) {
          error(te.loc, "un parámetro de la función no puede ser void", "usa un tipo concreto, p. ej. fn(int) -> int");
          return err();
        }
        return ft;
      }
    }
    return err();
  }

  // ------------------------------------------------------------ declaraciones

  void collectStructs() {
    for (auto& d : prog_.decls) {
      if (d->kind != DeclKind::Struct) continue;
      auto* sd = static_cast<StructDecl*>(d.get());
      if (isReservedName(sd->name) || structs_.count(sd->name) || funcs_.count(sd->name)) {
        if (isReservedName(sd->name)) {
          error(sd->loc, "'" + sd->name + "' es un nombre reservado", "elige otro nombre para la struct");
        } else {
          error(sd->loc, "struct '" + sd->name + "' ya definida", "elige otro nombre");
        }
        continue;
      }
      StructInfo si;
      si.decl = sd;
      si.type = types_.structOf(sd->name);
      structs_.emplace(sd->name, std::move(si));
    }
    for (auto& d : prog_.decls) {
      if (d->kind != DeclKind::Struct) continue;
      auto* sd = static_cast<StructDecl*>(d.get());
      auto it = structs_.find(sd->name);
      if (it == structs_.end() || it->second.decl != sd) continue;  // duplicada: ya reportada
      for (size_t i = 0; i < sd->fields.size(); ++i) {
        FieldDecl& f = sd->fields[i];
        for (size_t j = 0; j < i; ++j) {
          if (sd->fields[j].name == f.name) {
            error(f.loc, "campo '" + f.name + "' repetido en '" + sd->name + "'", "elimina uno de los dos campos");
          }
        }
        if (!f.annot) {
          error(f.loc, "falta el tipo del campo '" + f.name + "'", "escribe '" + f.name + ": int' (o el tipo que corresponda)");
          f.type = err();
        } else {
          f.type = resolveType(*f.annot);
        }
      }
    }
  }

  // Firma de función: tipos de parámetros, retorno y nombre LLVM. selfT != nullptr => método.
  void resolveSignature(FnDecl& fd, const Type* selfT) {
    const bool isMethod = selfT != nullptr;
    for (size_t i = 0; i < fd.params.size(); ++i) {
      Param& p = fd.params[i];
      if (p.isSelf) {
        if (!isMethod) {
          error(p.loc, "'self' solo es válido en los métodos de un impl", "quita 'self' o mueve la función dentro de un impl");
          p.type = err();
        } else {
          if (i != 0) error(p.loc, "'self' debe ser el primer parámetro", "mueve 'self' al principio de la lista");
          p.type = selfT;
        }
        continue;
      }
      if (!p.annot) {
        error(p.loc, "falta el tipo del parámetro '" + p.name + "'",
              "escribe '" + p.name + ": int' (o el tipo que corresponda)");
        p.type = err();
        continue;
      }
      p.type = resolveType(*p.annot);
    }
    if (isMethod && (fd.params.empty() || !fd.params[0].isSelf)) {
      error(fd.loc, "el método '" + fd.name + "' debe recibir 'self' como primer parámetro",
            "añade 'self' al principio de los parámetros");
    }
    fd.retType = fd.retAnnot ? resolveType(*fd.retAnnot) : types_.voidT();
    if (fd.isAsync && (!fd.retType || isVoid(fd.retType))) {
      error(fd.loc, "'async fn " + fd.name + "' necesita un tipo de retorno",
            "escribe: async fn " + fd.name + "(...) -> tipo:");
    }
  }

  void collectSignatures() {
    for (auto& d : prog_.decls) {
      if (d->kind == DeclKind::Fn) {
        auto* fd = static_cast<FnDecl*>(d.get());
        fd->mangled = fd->isExtern ? fd->name : "u_" + fd->name;
        resolveSignature(*fd, nullptr);
        if (isReservedName(fd->name)) {
          error(fd->loc, "'" + fd->name + "' es un nombre reservado", "elige otro nombre para la función");
        } else if (funcs_.count(fd->name) || structs_.count(fd->name)) {
          error(fd->loc, "'" + fd->name + "' ya está definida", "elige otro nombre para la función");
        } else {
          funcs_.emplace(fd->name, fd);
        }
      } else if (d->kind == DeclKind::Impl) {
        auto* im = static_cast<ImplDecl*>(d.get());
        auto it = structs_.find(im->typeName);
        const bool known = it != structs_.end();
        if (!known) {
          error(im->typeLoc, "impl de tipo desconocido '" + im->typeName + "'", "declara primero 'struct " + im->typeName + "'");
        }
        const Type* selfT = known ? it->second.type : err();
        for (auto& m : im->methods) {
          m->implOf = im->typeName;
          m->mangled = "u_" + im->typeName + "_" + m->name;
          resolveSignature(*m, selfT);
          if (!known) continue;
          if (m->isExtern) {
            error(m->loc, "los métodos no pueden ser extern", "quita 'extern' del método");
            continue;
          }
          if (it->second.methods.count(m->name)) {
            error(m->loc, "el método '" + m->name + "' ya está definido en '" + im->typeName + "'", "elige otro nombre");
            continue;
          }
          if (fieldIndex(it->second.decl, m->name) >= 0) {
            error(m->loc, "'" + m->name + "' es un campo de '" + im->typeName + "'", "un método no puede tener el nombre de un campo");
            continue;
          }
          it->second.methods[m->name] = m.get();
        }
      }
    }
  }

  // ------------------------------------------------------------ cuerpos de función

  void checkBody(FnDecl& fd) {
    if (fd.isExtern) return;
    const size_t savedBase = fnBase_;
    const Type* savedRet = curRet_;
    const bool savedIn = inFn_;
    const int savedLoop = loopDepth_;
    if (frames_.empty()) endedNames_.clear();  // una función de nivel superior empieza sin historia

    const size_t base = scopes_.size();
    scopes_.emplace_back();
    fnBase_ = base;
    curRet_ = fd.retType;
    inFn_ = true;
    loopDepth_ = 0;
    frames_.push_back(Frame{&fd, base});

    for (auto& p : fd.params) {
      if (p.isSelf) {
        declare("self", p.type, p.selfMut, p.loc, true);
        auto it = scopes_.back().find("self");
        if (it != scopes_.back().end()) it->second.captured = &p.captured;
      } else {
        declare(p.name, p.type, false, p.loc, true);
      }
    }
    checkStmts(fd.body);

    if (!bad(fd.retType) && !isVoid(fd.retType) && !blockReturns(fd.body)) {
      error(fd.loc, "la función '" + fd.name + "' no devuelve un valor en todos los caminos",
            "termina la función con 'return <valor>'");
    }

    frames_.pop_back();
    while (scopes_.size() > base) scopes_.pop_back();
    fnBase_ = savedBase;
    curRet_ = savedRet;
    inFn_ = savedIn;
    loopDepth_ = savedLoop;
  }

  void checkStmts(std::vector<StmtPtr>& body) {
    for (auto& s : body) {
      if (s) checkStmt(*s);
    }
  }

  void checkBlock(std::vector<StmtPtr>& body) {
    scopes_.emplace_back();
    checkStmts(body);
    popBlockScope();
  }

  // ------------------------------------------------------------ sentencias

  void checkStmt(Stmt& s) {
    switch (s.kind) {
      case StmtKind::VarDecl:
        checkVarDecl(static_cast<VarDecl&>(s));
        break;
      case StmtKind::Assign:
        checkAssign(static_cast<Assign&>(s));
        break;
      case StmtKind::ExprStmt: {
        auto& es = static_cast<ExprStmt&>(s);
        if (es.expr) checkExpr(*es.expr, nullptr);
        break;
      }
      case StmtKind::Return:
        checkReturn(static_cast<ReturnStmt&>(s));
        break;
      case StmtKind::If: {
        auto& i = static_cast<IfStmt&>(s);
        checkCond(*i.cond, "la condición de 'if'");
        checkBlock(i.thenBody);
        checkBlock(i.elseBody);
        break;
      }
      case StmtKind::For:
        checkFor(static_cast<ForStmt&>(s));
        break;
      case StmtKind::While: {
        auto& w = static_cast<WhileStmt&>(s);
        checkCond(*w.cond, "la condición de 'while'");
        ++loopDepth_;
        checkBlock(w.body);
        --loopDepth_;
        break;
      }
      case StmtKind::Break:
        if (loopDepth_ == 0) error(s.loc, "'break' fuera de un bucle", "usa break solo dentro de for o while");
        break;
      case StmtKind::Continue:
        if (loopDepth_ == 0) error(s.loc, "'continue' fuera de un bucle", "usa continue solo dentro de for o while");
        break;
      case StmtKind::Fn:
        checkNestedFn(static_cast<FnStmt&>(s));
        break;
    }
  }

  // `fn` dentro de un bloque: variable inmutable de tipo fn(...) en el ámbito actual, visible
  // solo dentro de ese bloque (también dentro de sí misma, para la recursión).
  void checkNestedFn(FnStmt& fs) {
    FnDecl& fd = *fs.fn;
    fd.mangled = "lc" + std::to_string(++closureCount_) + "_" + fd.name;
    fd.captures.clear();
    resolveSignature(fd, nullptr);
    std::vector<const Type*> ps;
    for (const auto& p : fd.params) ps.push_back(p.type ? p.type : err());
    fd.funcType = types_.funcOf(ps, fd.retType ? fd.retType : types_.voidT());
    VarInfo info;
    info.type = fd.funcType;
    info.isMut = false;
    info.selfOf = &fd;
    declareInfo(fd.name, info, fd.loc);
    checkBody(fd);
  }

  void checkVarDecl(VarDecl& v) {
    const Type* annot = v.annot ? resolveType(*v.annot) : nullptr;
    v.type = annot ? annot : err();
    if (v.init) {
      const Type* it = checkExpr(*v.init, annot);
      if (annot) {
        checkAssignable(annot, it, v.init->loc);
      } else if (isVoid(it)) {
        error(v.init->loc, "no se puede asignar un valor void a '" + v.name + "'", "la función no devuelve valor");
        v.type = err();
      } else {
        v.type = it;
      }
    }
    declare(v.name, v.type, v.isMut, v.loc);
    auto it = scopes_.back().find(v.name);
    if (it != scopes_.back().end()) {
      it->second.captured = &v.captured;
      // `mut` de nivel superior: si una función la usa, pasa a ser global LLVM (ver findVisible).
      if (v.isMut && scopes_.size() == 1) it->second.global = &v.isGlobal;
    }
  }

  void checkAssign(Assign& a) {
    Expr& target = *a.target;
    if (target.kind == ExprKind::Ident) {
      auto& id = static_cast<Ident&>(target);
      if (const VarInfo* found = findVisible(id.name)) {
        // Reasignación de una variable visible.
        VarInfo info = *found;
        id.type = info.type;
        a.declares = false;
        const Type* vt = checkExpr(*a.value, info.type);
        if (!info.isMut) {
          std::string hint = info.isParam
                                 ? "los parámetros no se reasignan; copia el valor en una variable 'mut'"
                                 : "usa 'mut " + id.name + "' para poder reasignarla";
          error(target.loc, "variable inmutable '" + id.name + "'", hint);
        }
        checkAssignable(info.type, vt, a.value->loc);
        return;
      }
      // Declaración nueva.
      a.declares = true;
      const Type* vt = checkExpr(*a.value, nullptr);
      const Type* t = vt;
      if (isVoid(vt)) {
        error(a.value->loc, "no se puede asignar un valor void a '" + id.name + "'", "la función no devuelve valor");
        t = err();
      }
      id.type = t;
      declare(id.name, t, false, target.loc);
      return;
    }
    if (target.kind == ExprKind::Member || target.kind == ExprKind::Index) {
      const Type* tt = checkExpr(target, nullptr);
      const Type* vt = checkExpr(*a.value, tt);
      a.declares = false;
      std::string root;
      const Place p = mutablePlace(target, root);
      if (p == Place::Immutable) {
        error(target.loc, "variable inmutable '" + root + "'",
              root == "self" ? "declara el método con 'mut self' para modificar sus campos"
                             : "usa 'mut " + root + "' para poder modificarla");
      } else if (p == Place::NotPlace) {
        error(target.loc, "no se puede asignar a esta expresión", "asigna a una variable, un campo o un elemento");
      }
      if (target.kind == ExprKind::Index && isTensor(tt)) {
        error(target.loc, "asignación a un tensor parcial", "indica todos los índices: t[i, j] = v");
        return;
      }
      checkAssignable(tt, vt, a.value->loc);
      return;
    }
    error(target.loc, "no se puede asignar a esta expresión", "asigna a una variable, un campo o un elemento");
    checkExpr(*a.value, nullptr);
  }

  // Indica si `e` es un lugar modificable. root recibe el nombre de la variable base.
  Place mutablePlace(const Expr& e, std::string& root) {
    if (bad(e.type)) return Place::Ok;  // error ya informado
    switch (e.kind) {
      case ExprKind::Ident: {
        const auto& id = static_cast<const Ident&>(e);
        const VarInfo* vi = findVisible(id.name);
        if (!vi || vi->isMut) return Place::Ok;
        root = id.name;
        return Place::Immutable;
      }
      case ExprKind::Member: {
        const auto& m = static_cast<const Member&>(e);
        if (!m.isField) return Place::NotPlace;
        return mutablePlace(*m.object, root);
      }
      case ExprKind::Index:
        return mutablePlace(*static_cast<const Index&>(e).object, root);
      default:
        return Place::NotPlace;
    }
  }

  void checkReturn(ReturnStmt& r) {
    if (!inFn_) {
      if (r.value) checkExpr(*r.value, nullptr);
      error(r.loc, "'return' fuera de una función", "el return solo es válido dentro de fn o de un método");
      return;
    }
    if (!r.value) {
      if (!bad(curRet_) && !isVoid(curRet_)) {
        error(r.loc, "falta el valor de retorno", "la función devuelve " + curRet_->str());
      }
      return;
    }
    const Type* vt = checkExpr(*r.value, curRet_);
    if (bad(curRet_)) return;
    if (isVoid(curRet_)) {
      error(r.value->loc, "la función no declara tipo de retorno",
            "declara el tipo de retorno en la firma para devolver un valor");
      return;
    }
    checkAssignable(curRet_, vt, r.value->loc);
  }

  void checkFor(ForStmt& f) {
    const Type* vt = err();
    if (f.iter->kind == ExprKind::Range) {
      auto& r = static_cast<RangeExpr&>(*f.iter);
      const Type* a = checkExpr(*r.start, types_.intT());
      const Type* b = checkExpr(*r.end, types_.intT());
      checkAssignable(types_.intT(), a, r.start->loc);
      checkAssignable(types_.intT(), b, r.end->loc);
      r.type = types_.intT();
      vt = types_.intT();
    } else {
      const Type* t = checkExpr(*f.iter, nullptr);
      if (!bad(t) && t->kind == TypeKind::List) {
        vt = t->elem;
      } else if (!bad(t)) {
        error(f.iter->loc, "'for' necesita un rango o una List, recibido " + t->str(),
              "usa 'for x in 0..n' o 'for x in lista'");
      }
    }
    f.varType = vt;
    scopes_.emplace_back();
    declare(f.var, vt, false, f.loc);
    ++loopDepth_;
    checkStmts(f.body);
    --loopDepth_;
    popBlockScope();
  }

  // ------------------------------------------------------------ expresiones

  const Type* checkExpr(Expr& e, const Type* hint) {
    const Type* t = exprType(e, hint);
    if (!t) t = err();
    e.type = t;
    return t;
  }

  const Type* exprType(Expr& e, const Type* hint) {
    switch (e.kind) {
      case ExprKind::IntLit: return types_.intT();
      case ExprKind::FloatLit: return types_.floatT();
      case ExprKind::BoolLit: return types_.boolT();
      case ExprKind::StrLit: return types_.strT();
      case ExprKind::Ident: return identType(static_cast<Ident&>(e));
      case ExprKind::Unary: return unaryType(static_cast<Unary&>(e));
      case ExprKind::Binary: return binaryType(static_cast<Binary&>(e));
      case ExprKind::Call: return callType(static_cast<Call&>(e), hint);
      case ExprKind::NoneLit:
        if (hint && hint->kind == TypeKind::Option) return hint;
        error(e.loc, "no se puede saber el tipo de None",
              "anótalo con un tipo Option, p. ej. x: Option[int] = None, o pásalo donde se espere un Option");
        return err();
      case ExprKind::Await: {
        auto& a = static_cast<AwaitExpr&>(e);
        const Type* ht = checkExpr(*a.operand, nullptr);
        if (bad(ht)) return err();
        if (ht->kind != TypeKind::Hilo) {
          error(e.loc, "'await' solo admite un Hilo[T], recibido " + ht->str(),
                "llama antes a una función async o lanza la función con 'hilo f(args)'");
          return err();
        }
        return ht->elem;
      }
      case ExprKind::Index: return indexType(static_cast<Index&>(e));
      case ExprKind::Member: return memberType(static_cast<Member&>(e));
      case ExprKind::ListLit: return listType(static_cast<ListLit&>(e), hint);
      case ExprKind::DictLit: return dictType(static_cast<DictLit&>(e), hint);
      case ExprKind::Range: {
        auto& r = static_cast<RangeExpr&>(e);
        if (r.start) checkExpr(*r.start, types_.intT());
        if (r.end) checkExpr(*r.end, types_.intT());
        error(e.loc, "un rango solo es válido como iterable de 'for'", "usa 'for i in a..b'");
        return err();
      }
    }
    return err();
  }

  const Type* identType(Ident& id) {
    if (const VarInfo* vi = findVisible(id.name)) return vi->type;
    if (inFn_ && scopes_[0].count(id.name)) {
      error(id.loc, "la función no puede usar la variable global '" + id.name + "'",
            "pásala como parámetro");
      return err();
    }
    if (structs_.count(id.name)) {
      error(id.loc, "'" + id.name + "' es una struct, no un valor",
            "crea una instancia con " + id.name + "(campo: valor, ...)");
      return err();
    }
    if (funcs_.count(id.name) || isBuiltinName(id.name)) {
      error(id.loc, "'" + id.name + "' no es un valor", "llámala con paréntesis: " + id.name + "(...)");
      return err();
    }
    if (endedNames_.count(id.name)) {
      error(id.loc, "variable no definida '" + id.name + "': su ámbito ya terminó",
            "la variable solo existe dentro del bloque donde se declaró; declárala de nuevo o pásala como parámetro");
      return err();
    }
    error(id.loc, "variable no definida '" + id.name + "'", "decláralo antes de usarlo, por ejemplo: " + id.name + " = 0");
    return err();
  }

  const Type* unaryType(Unary& u) {
    const Type* t = checkExpr(*u.operand, nullptr);
    if (bad(t)) return err();
    if (u.op == UnaryOp::Neg) {
      if (t == types_.intT() || t == types_.floatT()) return t;
      error(u.loc, "'-' requiere int o float, recibido " + t->str());
      return err();
    }
    if (t == types_.boolT()) return t;
    error(u.loc, "'not' requiere bool, recibido " + t->str(), "usa not solo con valores bool");
    return err();
  }

  void mixError(const Loc& loc, const std::string& op) {
    error(loc, "no se mezclan int y float en '" + op + "'", "usa float(x) para convertir el int a float");
  }

  void opError(const Loc& loc, const std::string& op, const Type* l, const Type* r) {
    std::string hint;
    if (l == types_.strT() || r == types_.strT()) hint = "usa str(x) para convertir un valor a texto";
    error(loc, "operador '" + op + "' no admite " + l->str() + " y " + r->str(), hint);
  }

  const Type* tensorElementwise(const Loc& loc, const std::string& op, const Type* l, const Type* r) {
    if (l->elem != r->elem) {
      error(loc, "tensores de distinto tipo de elemento en '" + op + "': " + l->str() + " y " + r->str(),
            "convierte ambos a Tensor[float]");
      return err();
    }
    if (!TypeTable::compatible(l, r)) {
      error(loc, "formas de tensor incompatibles en '" + op + "': " + l->str() + " y " + r->str(),
            "las dimensiones de ambos tensores deben coincidir");
      return err();
    }
    return types_.tensorOf(l->elem, mergeShape(l->shape, r->shape));
  }

  // Tensor op escalar o escalar op Tensor (+ - * /). Devuelve nullptr si la combinación no es de este tipo.
  // El resultado es el tensor (misma forma y tipo de elemento). El escalar debe ser del tipo del elemento.
  const Type* tensorScalarType(const Loc& loc, const std::string& op, const Type* l, const Type* r) {
    if (isTensor(l) == isTensor(r)) return nullptr;
    const Type* t = isTensor(l) ? l : r;
    const Type* s = isTensor(l) ? r : l;
    if (s != types_.intT() && s != types_.floatT()) return nullptr;
    if (s != t->elem) {
      const bool f = s == types_.floatT();
      error(loc, "no se mezclan " + s->str() + " y " + t->str() + " en '" + op + "'",
            f ? "usa float(x) o un tensor de float" : "usa int(x) o un tensor de int");
      return err();
    }
    return t;
  }

  // Suma, resta y multiplicación: escalares del mismo tipo, str (solo +), o tensores elemento a elemento.
  const Type* arith(const Loc& loc, const std::string& op, const Type* l, const Type* r, bool allowStr) {
    if (isTensor(l) && isTensor(r)) return tensorElementwise(loc, op, l, r);
    if (const Type* ts = tensorScalarType(loc, op, l, r)) return ts;
    if (l == r && (l == types_.intT() || l == types_.floatT())) return l;
    if (allowStr && l == types_.strT() && r == types_.strT()) return l;
    if (isNumMix(l, r)) {
      mixError(loc, op);
      return err();
    }
    opError(loc, op, l, r);
    return err();
  }

  const Type* matmulType(const Loc& loc, const Type* l, const Type* r) {
    if (!isTensor(l) || !isTensor(r)) {
      error(loc, "'@' requiere dos tensores, recibido " + l->str() + " y " + r->str(), "usa tensores de Tensor[float]");
      return err();
    }
    if (l->elem != types_.floatT() || r->elem != types_.floatT()) {
      error(loc, "'@' requiere Tensor[float]", "crea los tensores con zeros, ones, randn o from_list(List[float], ...)");
      return err();
    }
    if (l->shape.size() != 2 || r->shape.size() != 2) {
      error(loc, "'@' requiere tensores de rango 2, recibido " + l->str() + " y " + r->str());
      return err();
    }
    const int64_t k1 = l->shape[1];
    const int64_t k2 = r->shape[0];
    if (k1 >= 0 && k2 >= 0 && k1 != k2) {
      error(loc, "formas incompatibles para '@': " + l->str() + " y " + r->str(),
            "las columnas de la izquierda deben coincidir con las filas de la derecha");
      return err();
    }
    return types_.tensorOf(types_.floatT(), {l->shape[0], r->shape[1]});
  }

  const Type* binaryType(Binary& b) {
    const bool eqOp = b.op == BinaryOp::Eq || b.op == BinaryOp::Ne;
    const Type* l = nullptr;
    const Type* r = nullptr;
    if (eqOp && b.lhs->kind == ExprKind::NoneLit && b.rhs->kind != ExprKind::NoneLit) {
      r = checkExpr(*b.rhs, nullptr);
      l = checkExpr(*b.lhs, r);
    } else {
      l = checkExpr(*b.lhs, nullptr);
      r = checkExpr(*b.rhs, eqOp && b.rhs->kind == ExprKind::NoneLit ? l : nullptr);
    }
    if (bad(l) || bad(r)) return err();
    if (isVoid(l) || isVoid(r)) {
      error(b.loc, "no se puede usar un valor void en una operación", "la función no devuelve valor");
      return err();
    }
    const Type* I = types_.intT();
    const Type* F = types_.floatT();
    switch (b.op) {
      case BinaryOp::Add: return arith(b.loc, "+", l, r, true);
      case BinaryOp::Sub: return arith(b.loc, "-", l, r, false);
      case BinaryOp::Mul: return arith(b.loc, "*", l, r, false);
      case BinaryOp::Div: {
        if (const Type* ts = tensorScalarType(b.loc, "/", l, r)) return ts;
        if (l == I && r == I) return I;
        if (l == F && r == F) return F;
        if (isNumMix(l, r)) {
          mixError(b.loc, "/");
          return err();
        }
        opError(b.loc, "/", l, r);
        return err();
      }
      case BinaryOp::Mod: {
        if (l == I && r == I) return I;
        if (isNumMix(l, r)) {
          mixError(b.loc, "%");
          return err();
        }
        error(b.loc, "'%' solo admite int, recibido " + l->str() + " y " + r->str(), "el operador % no está definido para float");
        return err();
      }
      case BinaryOp::MatMul:
        return matmulType(b.loc, l, r);
      case BinaryOp::Eq:
      case BinaryOp::Ne: {
        const char* op = b.op == BinaryOp::Eq ? "==" : "!=";
        if (l->kind == TypeKind::Option && (b.lhs->kind == ExprKind::NoneLit || b.rhs->kind == ExprKind::NoneLit)) {
          return types_.boolT();  // opt == None / opt != None
        }
        if (isNumMix(l, r)) {
          mixError(b.loc, op);
          return err();
        }
        if (l != r) {
          error(b.loc, "no se pueden comparar " + l->str() + " y " + r->str(), "ambos operandos deben ser del mismo tipo");
          return err();
        }
        if (l == I || l == F || l == types_.boolT() || l == types_.strT()) return types_.boolT();
        if (l->kind == TypeKind::Struct) {
          if (hasMethod(l->name, "eq")) return types_.boolT();
          error(b.loc, "no se puede comparar '" + l->name + "' con " + op,
                "define un método 'eq' en el impl de " + l->name + " para poder compararlo");
          return err();
        }
        error(b.loc, "no se puede comparar " + l->str() + " con " + op, "compara sus elementos o campos");
        return err();
      }
      case BinaryOp::Lt:
      case BinaryOp::Gt:
      case BinaryOp::Le:
      case BinaryOp::Ge: {
        const char* op = b.op == BinaryOp::Lt ? "<" : b.op == BinaryOp::Gt ? ">" : b.op == BinaryOp::Le ? "<=" : ">=";
        if (l == r && (l == I || l == F || l == types_.strT())) return types_.boolT();
        if (isNumMix(l, r)) {
          mixError(b.loc, op);
          return err();
        }
        opError(b.loc, op, l, r);
        return err();
      }
      case BinaryOp::And:
      case BinaryOp::Or: {
        const char* op = b.op == BinaryOp::And ? "and" : "or";
        if (l == types_.boolT() && r == types_.boolT()) return types_.boolT();
        error(b.loc, std::string("'") + op + "' requiere bool, recibido " + l->str() + " y " + r->str(),
              "usa comparaciones que devuelvan bool");
        return err();
      }
      case BinaryOp::In: {
        if (r->kind == TypeKind::Dict) {
          if (!checkAssignable(r->key, l, b.lhs->loc)) return err();
          return types_.boolT();
        }
        if (r->kind == TypeKind::List) {
          if (!checkAssignable(r->elem, l, b.lhs->loc)) return err();
          return types_.boolT();
        }
        error(b.loc, "'in' requiere un Dict o una List a la derecha, recibido " + r->str());
        return err();
      }
    }
    return err();
  }

  // Valor entero de un literal (5, -5); false si la expresión no es un literal entero.
  static bool literalInt(const Expr& e, int64_t& out) {
    if (e.kind == ExprKind::IntLit) {
      out = static_cast<const IntLit&>(e).value;
      return true;
    }
    if (e.kind == ExprKind::Unary) {
      const auto& u = static_cast<const Unary&>(e);
      if (u.op == UnaryOp::Neg && u.operand->kind == ExprKind::IntLit) {
        out = -static_cast<const IntLit&>(*u.operand).value;
        return true;
      }
    }
    return false;
  }

  // t[i, j, ...] sobre un tensor: índice completo -> elemento; parcial -> tensor de la sub-forma restante.
  const Type* tensorIndexType(Index& ix, const Type* ot) {
    const size_t n = 1 + ix.rest.size();
    const size_t rank = ot->shape.size();
    std::vector<Expr*> all{ix.index.get()};
    for (auto& e : ix.rest) all.push_back(e.get());
    for (Expr* e : all) {
      const Type* it = checkExpr(*e, types_.intT());
      if (bad(it)) return err();
      if (!checkAssignable(types_.intT(), it, e->loc)) return err();
    }
    if (n > rank) {
      error(ix.loc, "demasiados índices para un tensor de rango " + std::to_string(rank) + ": " + std::to_string(n) +
                        " índices recibidos", "un tensor de rango " + std::to_string(rank) + " admite como máximo " +
                                                  std::to_string(rank) + " índices");
      return err();
    }
    // Comprobación en compilación de los índices literales frente a la forma conocida.
    for (size_t d = 0; d < n; d++) {
      int64_t v = 0;
      const int64_t dim = ot->shape[d];
      if (dim < 0 || !literalInt(*all[d], v)) continue;
      if (v < -dim || v >= dim) {
        error(all[d]->loc, "índice " + std::to_string(v) + " fuera de rango (dimensión " + std::to_string(d) +
                               " de tamaño " + std::to_string(dim) + ")",
              "los índices válidos van de -" + std::to_string(dim) + " a " + std::to_string(dim - 1));
        return err();
      }
    }
    if (n == rank) return ot->elem;
    std::vector<int64_t> rest(ot->shape.begin() + static_cast<std::ptrdiff_t>(n), ot->shape.end());
    return types_.tensorOf(ot->elem, rest);
  }

  const Type* indexType(Index& ix) {
    const Type* ot = checkExpr(*ix.object, nullptr);
    if (bad(ot)) {
      checkExpr(*ix.index, nullptr);
      for (auto& e : ix.rest) checkExpr(*e, nullptr);
      return err();
    }
    if (ot->kind == TypeKind::Tensor) return tensorIndexType(ix, ot);
    if (!ix.rest.empty()) {
      checkExpr(*ix.index, nullptr);
      for (auto& e : ix.rest) checkExpr(*e, nullptr);
      error(ix.loc, "demasiados índices para " + ot->str(), "solo los tensores admiten varios índices: t[i, j]");
      return err();
    }
    if (ot->kind == TypeKind::List) {
      const Type* it = checkExpr(*ix.index, types_.intT());
      checkAssignable(types_.intT(), it, ix.index->loc);
      return ot->elem;
    }
    if (ot->kind == TypeKind::Dict) {
      const Type* kt = checkExpr(*ix.index, ot->key);
      checkAssignable(ot->key, kt, ix.index->loc);
      return ot->val;
    }
    checkExpr(*ix.index, nullptr);
    error(ix.loc, "no se puede indexar un valor de tipo " + ot->str(), "solo List y Dict admiten []");
    return err();
  }

  const Type* memberType(Member& m) {
    const Type* ot = checkExpr(*m.object, nullptr);
    if (bad(ot)) return err();
    if (ot->kind == TypeKind::Struct) {
      auto sit = structs_.find(ot->name);
      if (sit != structs_.end()) {
        const int idx = fieldIndex(sit->second.decl, m.name);
        if (idx >= 0) {
          m.isField = true;
          return sit->second.decl->fields[static_cast<size_t>(idx)].type;
        }
        if (sit->second.methods.count(m.name)) {
          error(m.loc, "'" + m.name + "' es un método de '" + ot->name + "'",
                "llámalo con paréntesis: objeto." + m.name + "(...)");
          return err();
        }
        error(m.loc, "la struct '" + ot->name + "' no tiene el campo '" + m.name + "'",
              "campos: " + joinFields(sit->second.decl));
        return err();
      }
      return err();
    }
    error(m.loc, "no se puede acceder a '." + m.name + "' en un valor de tipo " + ot->str(),
          "si es un método, llámalo con paréntesis: " + m.name + "()");
    return err();
  }

  const Type* listType(ListLit& l, const Type* hint) {
    const Type* hintElem = (hint && hint->kind == TypeKind::List) ? hint->elem : nullptr;
    if (l.elems.empty()) {
      if (hintElem) return hint;
      error(l.loc, "la lista vacía necesita un tipo", "anótalo: x: List[int] = []");
      return err();
    }
    if (hintElem) {
      for (auto& el : l.elems) {
        if (!el) continue;
        const Type* t = checkExpr(*el, hintElem);
        checkAssignable(hintElem, t, el->loc);
      }
      return hint;
    }
    const Type* et = nullptr;
    for (auto& el : l.elems) {
      if (!el) continue;
      const Type* t = checkExpr(*el, et);
      if (bad(t)) continue;
      if (isVoid(t)) {
        error(el->loc, "no se puede poner un valor void en una lista", "la función no devuelve valor");
        continue;
      }
      if (!et) {
        et = t;
      } else {
        checkAssignable(et, t, el->loc);
      }
    }
    if (!et) return err();
    return types_.listOf(et);
  }

  const Type* dictType(DictLit& d, const Type* hint) {
    const Type* hk = (hint && hint->kind == TypeKind::Dict) ? hint->key : nullptr;
    const Type* hv = (hint && hint->kind == TypeKind::Dict) ? hint->val : nullptr;
    if (d.entries.empty()) {
      if (hk) return hint;
      error(d.loc, "el diccionario vacío necesita un tipo", "anótalo: d: Dict[str, int] = {}");
      return err();
    }
    const Type* kt = hk;
    const Type* vt = hv;
    bool okAll = true;
    for (auto& [ke, ve] : d.entries) {
      if (!ke || !ve) continue;
      const Type* k = checkExpr(*ke, hk ? hk : kt);
      const Type* v = checkExpr(*ve, hv ? hv : vt);
      if (hk) {
        if (!checkAssignable(hk, k, ke->loc)) okAll = false;
      } else if (!bad(k)) {
        if (k != types_.intT() && k != types_.strT() && k != types_.boolT()) {
          error(ke->loc, "clave de Dict no válida: " + k->str(), "las claves pueden ser int, str o bool");
          okAll = false;
        } else if (!kt) {
          kt = k;
        } else if (!checkAssignable(kt, k, ke->loc)) {
          okAll = false;
        }
      } else {
        okAll = false;
      }
      if (hv) {
        if (!checkAssignable(hv, v, ve->loc)) okAll = false;
      } else if (!bad(v)) {
        if (!vt) {
          vt = v;
        } else if (!checkAssignable(vt, v, ve->loc)) {
          okAll = false;
        }
      } else {
        okAll = false;
      }
    }
    if (!okAll || !kt || !vt) return err();
    return types_.dictOf(kt, vt);
  }

  // ------------------------------------------------------------ llamadas

  void checkAll(Call& c) {
    for (auto& a : c.args) {
      if (a) checkExpr(*a, nullptr);
    }
  }

  // Devuelve false (y lo informa) si la llamada tiene argumentos con nombre.
  bool noNamedArgs(Call& c, const std::string& what) {
    for (size_t i = 0; i < c.args.size() && i < c.argNames.size(); ++i) {
      if (!c.argNames[i].empty()) {
        error(c.args[i]->loc, what + " no admite argumentos con nombre", "quita el nombre del argumento");
        return false;
      }
    }
    return true;
  }

  // Devuelve false (y lo informa, comprobando los argumentos) si el número no coincide.
  bool expectArgs(Call& c, size_t n, const std::string& name) {
    if (c.args.size() == n) return true;
    error(c.loc, "'" + name + "' espera " + std::to_string(n) + " argumento(s), recibió " + std::to_string(c.args.size()));
    checkAll(c);
    return false;
  }

  void checkArgs(Call& c, const std::vector<const Type*>& params, const std::string& name) {
    if (c.args.size() != params.size()) {
      error(c.loc, "'" + name + "' espera " + std::to_string(params.size()) + " argumento(s), recibió " +
                       std::to_string(c.args.size()));
      checkAll(c);
      return;
    }
    for (size_t i = 0; i < params.size(); ++i) {
      if (!c.args[i]) continue;
      const Type* at = checkExpr(*c.args[i], params[i]);
      checkAssignable(params[i], at, c.args[i]->loc);
    }
  }

  // hilo f(args): f debe ser una función de nivel superior con retorno (no clausura, builtin, struct ni método).
  const Type* spawnCall(Call& c) {
    if (c.callee->kind != ExprKind::Ident) {
      error(c.loc, "'hilo' solo admite funciones de nivel superior", "escribe: hilo f(args)");
      checkAll(c);
      return err();
    }
    const std::string name = static_cast<Ident&>(*c.callee).name;
    auto fit = funcs_.find(name);
    if (fit == funcs_.end() || fit->second->isExtern) {
      error(c.loc, "'hilo' solo admite funciones de nivel superior definidas con 'fn'",
            "'" + name + "' no es una función de nivel superior");
      checkAll(c);
      return err();
    }
    FnDecl* fd = fit->second;
    if (fd->isAsync) {
      error(c.loc, "'" + name + "' es async: su llamada ya devuelve un Hilo", "quita 'hilo' de la llamada");
      checkAll(c);
      return err();
    }
    spawns_.push_back(SpawnSite{fd, c.loc});
    if (!fd->retType || isVoid(fd->retType)) {
      error(c.loc, "'hilo' requiere una función que devuelva un valor",
            "añade '-> tipo' a la firma de '" + name + "'");
      checkAll(c);
      return err();
    }
    noNamedArgs(c, "'hilo " + name + "'");
    std::vector<const Type*> ps;
    for (const auto& p : fd->params) ps.push_back(p.type);
    checkArgs(c, ps, name);
    c.callKind = CallKind::Spawn;
    c.target = fd->mangled;
    return types_.hiloOf(fd->retType);
  }

  const Type* callType(Call& c, const Type* hint) {
    if (c.spawn) return spawnCall(c);
    const Type* res = err();
    if (c.callee->kind == ExprKind::Member) {
      res = methodCall(c);
    } else if (c.callee->kind == ExprKind::Ident) {
      res = namedCall(c, hint);
    } else {
      // Cualquier otra expresión de tipo función, p. ej. crear()(3) o fs[0](3).
      const Type* ft = checkExpr(*c.callee, nullptr);
      if (bad(ft)) {
        checkAll(c);
        return err();
      }
      if (ft->kind != TypeKind::Func) {
        error(c.loc, "esta expresión no se puede llamar", "llama a una función, a un método o a un valor de tipo fn(...)");
        checkAll(c);
        return err();
      }
      res = closureCall(c, ft);
    }
    if (!res) res = err();
    if (c.callKind != CallKind::Closure) c.callee->type = res;  // el tipo del callee es el tipo de retorno de la llamada
    return res;
  }

  // Llamada a un valor de tipo fn(...): la función anidada que lo almacena se invoca con sus argumentos.
  const Type* closureCall(Call& c, const Type* ft) {
    c.callKind = CallKind::Closure;
    c.callee->type = ft;
    noNamedArgs(c, "una función");
    std::string who = "la función";
    if (c.callee->kind == ExprKind::Ident) who = static_cast<Ident&>(*c.callee).name;
    checkArgs(c, ft->params, who);
    return ft->ret;
  }

  const Type* namedCall(Call& c, const Type* hint) {
    const std::string name = static_cast<Ident&>(*c.callee).name;
    auto fit = funcs_.find(name);
    if (fit != funcs_.end()) {
      FnDecl* fd = fit->second;
      if (fd->isAsync) {  // llamar a una async fn lanza su hilo y devuelve Hilo[T]
        noNamedArgs(c, "la función async '" + name + "'");
        std::vector<const Type*> ps;
        for (const auto& p : fd->params) ps.push_back(p.type);
        checkArgs(c, ps, name);
        c.callKind = CallKind::Spawn;
        c.target = fd->mangled;
        spawns_.push_back(SpawnSite{fd, c.loc});
        return types_.hiloOf(fd->retType);
      }
      c.callKind = fd->isExtern ? CallKind::Extern : CallKind::Function;
      c.target = fd->mangled;
      noteCall(fd);
      noNamedArgs(c, "la función '" + name + "'");
      std::vector<const Type*> ps;
      for (const auto& p : fd->params) ps.push_back(p.type);
      checkArgs(c, ps, name);
      return fd->retType ? fd->retType : err();
    }
    auto sit = structs_.find(name);
    if (sit != structs_.end()) {
      c.callKind = CallKind::StructCtor;
      c.target = name;
      return structCtor(c, sit->second);
    }
    if (isBuiltinName(name)) return builtinCall(c, name, hint);
    if (VarInfo* vi = findVisible(name)) {
      const Type* vt = vi->type;
      if (!vt || vt->kind == TypeKind::Error) {
        checkAll(c);
        return err();
      }
      if (vi->selfOf) noteCall(vi->selfOf);  // llamada por nombre a una función anidada
      if (vt->kind == TypeKind::Func) return closureCall(c, vt);
      error(c.loc, "'" + name + "' es una variable, no una función", "las variables no se llaman con paréntesis");
    } else {
      error(c.loc, "función no definida '" + name + "'", "declárala con 'fn " + name + "(...)'");
    }
    checkAll(c);
    return err();
  }

  const Type* structCtor(Call& c, StructInfo& si) {
    const std::string& name = si.decl->name;
    const auto& fields = si.decl->fields;
    std::vector<bool> seen(fields.size(), false);
    for (size_t i = 0; i < c.args.size(); ++i) {
      if (!c.args[i]) continue;
      Expr& arg = *c.args[i];
      const std::string an = i < c.argNames.size() ? c.argNames[i] : std::string();
      if (an.empty()) {
        error(arg.loc, "los argumentos de '" + name + "' necesitan nombre", "usa " + name + "(campo: valor, ...)");
        checkExpr(arg, nullptr);
        continue;
      }
      const int idx = fieldIndex(si.decl, an);
      if (idx < 0) {
        error(arg.loc, "la struct '" + name + "' no tiene el campo '" + an + "'", "campos: " + joinFields(si.decl));
        checkExpr(arg, nullptr);
        continue;
      }
      const size_t ui = static_cast<size_t>(idx);
      if (seen[ui]) {
        error(arg.loc, "campo '" + an + "' repetido en '" + name + "'", "da valor a cada campo una sola vez");
        checkExpr(arg, nullptr);
        continue;
      }
      seen[ui] = true;
      const Type* ft = fields[ui].type;
      const Type* at = checkExpr(arg, ft);
      checkAssignable(ft, at, arg.loc);
    }
    std::string missing;
    std::string first;
    for (size_t i = 0; i < fields.size(); ++i) {
      if (seen[i]) continue;
      if (missing.empty()) first = fields[i].name;
      if (!missing.empty()) missing += ", ";
      missing += fields[i].name;
    }
    if (!missing.empty()) {
      error(c.loc, "faltan campos en '" + name + "': " + missing, "añade " + first + ": <valor>");
    }
    return si.type;
  }

  const Type* methodCall(Call& c) {
    auto& m = static_cast<Member&>(*c.callee);
    const Type* ot = checkExpr(*m.object, nullptr);
    m.isField = false;
    if (bad(ot)) {
      checkAll(c);
      return err();
    }

    if (ot->kind == TypeKind::Struct) {
      auto sit = structs_.find(ot->name);
      if (sit == structs_.end()) {
        checkAll(c);
        return err();
      }
      auto mit = sit->second.methods.find(m.name);
      if (mit == sit->second.methods.end()) {
        if (fieldIndex(sit->second.decl, m.name) >= 0) {
          error(m.loc, "'" + m.name + "' es un campo de '" + ot->name + "', no un método",
                "quita los paréntesis para leer el campo");
        } else {
          error(m.loc, "la struct '" + ot->name + "' no tiene el método '" + m.name + "'",
                "define 'fn " + m.name + "' en su impl");
        }
        checkAll(c);
        return err();
      }
      FnDecl* fd = mit->second;
      c.callKind = CallKind::Method;
      c.target = fd->mangled;
      noteCall(fd);
      if (!fd->params.empty() && fd->params[0].isSelf && fd->params[0].selfMut) {
        std::string root;
        const Place p = mutablePlace(*m.object, root);
        if (p == Place::Immutable) {
          error(c.loc, "'" + m.name + "' modifica el objeto y la variable '" + root + "' es inmutable",
                root == "self" ? "declara el método con 'mut self'" : "usa 'mut " + root + "' para poder modificarla");
        } else if (p == Place::NotPlace) {
          error(c.loc, "'" + m.name + "' modifica un valor temporal", "guarda el valor en una variable 'mut' antes");
        }
      }
      noNamedArgs(c, "el método '" + m.name + "'");
      std::vector<const Type*> ps;
      for (size_t i = 0; i < fd->params.size(); ++i) {
        if (!fd->params[i].isSelf) ps.push_back(fd->params[i].type);
      }
      checkArgs(c, ps, m.name);
      return fd->retType ? fd->retType : err();
    }

    if (ot->kind == TypeKind::Tensor) {
      if (m.name == "transpose") {
        if (!expectArgs(c, 0, "transpose")) return err();
        if (ot->shape.size() != 2) {
          error(c.loc, "transpose requiere un tensor de rango 2, recibido " + ot->str());
          return err();
        }
        c.callKind = CallKind::BuiltinMethod;
        c.target = "Tensor.transpose";
        return types_.tensorOf(ot->elem, {ot->shape[1], ot->shape[0]});
      }
      if (m.name == "forma") {
        if (!expectArgs(c, 0, "forma")) return err();
        c.callKind = CallKind::BuiltinMethod;
        c.target = "Tensor.forma";
        return types_.listOf(types_.intT());
      }
      if (m.name == "numel") {
        if (!expectArgs(c, 0, "numel")) return err();
        c.callKind = CallKind::BuiltinMethod;
        c.target = "Tensor.numel";
        return types_.intT();
      }
    }

    if (ot->kind == TypeKind::List) {
      if (m.name == "append") {
        if (!expectArgs(c, 1, "append")) return err();
        if (!requireMutObject(m, c, "append")) {
          checkAll(c);
          return types_.voidT();
        }
        const Type* at = checkExpr(*c.args[0], ot->elem);
        checkAssignable(ot->elem, at, c.args[0]->loc);
        c.callKind = CallKind::BuiltinMethod;
        c.target = "List.append";
        return types_.voidT();
      }
      if (m.name == "pop") {
        if (!expectArgs(c, 0, "pop")) return err();
        if (!requireMutObject(m, c, "pop")) return ot->elem;
        c.callKind = CallKind::BuiltinMethod;
        c.target = "List.pop";
        return ot->elem;
      }
      if (m.name == "len") {
        if (!expectArgs(c, 0, "len")) return err();
        c.callKind = CallKind::BuiltinMethod;
        c.target = "List.len";
        return types_.intT();
      }
    }

    if (ot->kind == TypeKind::Dict) {
      if (m.name == "tiene") {
        if (!expectArgs(c, 1, "tiene")) return err();
        const Type* kt = checkExpr(*c.args[0], ot->key);
        checkAssignable(ot->key, kt, c.args[0]->loc);
        c.callKind = CallKind::BuiltinMethod;
        c.target = "Dict.tiene";
        return types_.boolT();
      }
      if (m.name == "len") {
        if (!expectArgs(c, 0, "len")) return err();
        c.callKind = CallKind::BuiltinMethod;
        c.target = "Dict.len";
        return types_.intT();
      }
    }

    if (ot->kind == TypeKind::Option) {
      if (m.name == "es_some" || m.name == "es_none") {
        if (!expectArgs(c, 0, m.name)) return err();
        c.callKind = CallKind::BuiltinMethod;
        c.target = "Option." + m.name;
        return types_.boolT();
      }
      if (m.name == "desempacar_o") {
        if (!expectArgs(c, 1, m.name)) return err();
        const Type* at = checkExpr(*c.args[0], ot->elem);
        if (!checkAssignable(ot->elem, at, c.args[0]->loc)) return err();
        c.callKind = CallKind::BuiltinMethod;
        c.target = "Option.desempacar_o";
        return ot->elem;
      }
    }

    if (ot->kind == TypeKind::Hilo && m.name == "esperar") {
      if (!expectArgs(c, 0, m.name)) return err();
      c.callKind = CallKind::BuiltinMethod;
      c.target = "Hilo.esperar";
      return ot->elem;
    }
    if (ot->kind == TypeKind::Canal) {
      if (m.name == "enviar") {
        if (!expectArgs(c, 1, m.name)) return err();
        const Type* at = checkExpr(*c.args[0], ot->elem);
        checkAssignable(ot->elem, at, c.args[0]->loc);
        c.callKind = CallKind::BuiltinMethod;
        c.target = "Canal.enviar";
        return types_.voidT();
      }
      if (m.name == "recibir") {
        if (!expectArgs(c, 0, m.name)) return err();
        c.callKind = CallKind::BuiltinMethod;
        c.target = "Canal.recibir";
        return types_.optionOf(ot->elem);
      }
      if (m.name == "cerrar") {
        if (!expectArgs(c, 0, m.name)) return err();
        c.callKind = CallKind::BuiltinMethod;
        c.target = "Canal.cerrar";
        return types_.voidT();
      }
    }
    if (ot->kind == TypeKind::Debil && m.name == "fuerte") {
      if (!expectArgs(c, 0, m.name)) return err();
      c.callKind = CallKind::BuiltinMethod;
      c.target = "Debil.fuerte";
      return types_.optionOf(ot->elem);
    }

    error(m.loc, "el tipo " + ot->str() + " no tiene el método '" + m.name + "'",
          "revisa el nombre del método");
    checkAll(c);
    return err();
  }

  // Comprueba que el objeto de un método mutante es un lugar modificable.
  bool requireMutObject(const Member& m, const Call& c, const std::string& what) {
    std::string root;
    const Place p = mutablePlace(*m.object, root);
    if (p == Place::Immutable) {
      error(c.loc, "no se puede llamar a '" + what + "' sobre la variable inmutable '" + root + "'",
            root == "self" ? "declara el método con 'mut self'" : "usa 'mut " + root + "' para poder modificarla");
      return false;
    }
    if (p == Place::NotPlace) {
      error(c.loc, "no se puede llamar a '" + what + "' sobre un valor temporal", "guarda el valor en una variable 'mut' antes");
      return false;
    }
    return true;
  }

  // Dimensiones desde los argumentos a partir de `first`. Literales -> valor; si no -1.
  std::vector<int64_t> dimsFrom(Call& c, size_t first) {
    std::vector<int64_t> shape;
    for (size_t i = first; i < c.args.size(); ++i) {
      if (!c.args[i]) {
        shape.push_back(-1);
        continue;
      }
      Expr& d = *c.args[i];
      const Type* t = checkExpr(d, types_.intT());
      if (bad(t)) {
        shape.push_back(-1);
        continue;
      }
      if (t != types_.intT()) {
        error(d.loc, "las dimensiones deben ser int, recibido " + t->str(), "usa int(x) para convertir");
        shape.push_back(-1);
        continue;
      }
      if (d.kind == ExprKind::IntLit) {
        int64_t v = static_cast<IntLit&>(d).value;
        if (v < 0) {
          error(d.loc, "dimensión negativa " + std::to_string(v));
          v = -1;
        }
        shape.push_back(v);
      } else {
        shape.push_back(-1);
      }
    }
    return shape;
  }

  const Type* fromList(Call& c) {
    if (c.args.size() < 2 || !c.args[0]) {
      checkAll(c);
      error(c.loc, "from_list necesita una lista y al menos una dimensión", "escribe from_list(lista, 2, 3)");
      return err();
    }
    Expr& lst = *c.args[0];
    const Type* lt = checkExpr(lst, nullptr);
    std::vector<int64_t> shape = dimsFrom(c, 1);
    if (bad(lt)) return err();
    if (lt->kind != TypeKind::List || (lt->elem != types_.intT() && lt->elem != types_.floatT())) {
      error(lst.loc, "from_list espera List[int] o List[float], recibido " + lt->str(),
            "convierte los elementos con float(x) o int(x)");
      return err();
    }
    if (lst.kind == ExprKind::ListLit) {
      const size_t n = static_cast<ListLit&>(lst).elems.size();
      int64_t prod = 1;
      bool known = true;
      for (int64_t d : shape) {
        if (d < 0) {
          known = false;
        } else {
          prod *= d;
        }
      }
      if (known && static_cast<int64_t>(n) != prod) {
        error(c.loc, "from_list: la lista tiene " + std::to_string(n) + " elementos pero la forma requiere " +
                         std::to_string(prod),
              "ajusta la lista o las dimensiones");
      }
    }
    return types_.tensorOf(lt->elem, shape);
  }

  const Type* builtinCall(Call& c, const std::string& name, const Type* hint) {
    c.callKind = CallKind::Builtin;
    c.target = name;
    noNamedArgs(c, "'" + name + "'");

    if (name == "Canal") {
      if (c.typeArgs.size() != 1 || !c.typeArgs[0]) {
        error(c.loc, "Canal necesita el tipo de los mensajes", "escribe: Canal[int]()");
        checkAll(c);
        return err();
      }
      if (!c.args.empty()) {
        error(c.loc, "Canal[T]() no admite argumentos", "crea el canal vacío y envía valores con c.enviar(x)");
        checkAll(c);
        return err();
      }
      const Type* et = resolveType(*c.typeArgs[0]);
      if (bad(et)) return err();
      return types_.canalOf(et);
    }
    if (name == "debil") {
      if (!expectArgs(c, 1, name) || !c.args[0]) return err();
      const Type* t = checkExpr(*c.args[0], nullptr);
      if (bad(t)) return err();
      if (!t->isHeap()) {
        error(c.args[0]->loc, "debil() no admite " + t->str(),
              "solo se observan valores heap: str, List, Dict, Tensor, struct, Option o fn");
        return err();
      }
      return types_.debilOf(t);
    }

    if (name == "Some") {
      if (!expectArgs(c, 1, name) || !c.args[0]) return err();
      const Type* expect = (hint && hint->kind == TypeKind::Option) ? hint->elem : nullptr;
      const Type* at = checkExpr(*c.args[0], expect);
      if (bad(at)) return err();
      if (expect) {
        if (!checkAssignable(expect, at, c.args[0]->loc)) return err();
        return hint;
      }
      if (isVoid(at)) {
        error(c.args[0]->loc, "Some() no admite valores void", "la función no devuelve valor");
        return err();
      }
      return types_.optionOf(at);  // sin contexto: Option del tipo del argumento
    }

    if (name == "print") {
      for (auto& a : c.args) {
        if (!a) continue;
        const Type* t = checkExpr(*a, nullptr);
        if (isVoid(t)) error(a->loc, "print no admite valores void", "la función no devuelve valor");
      }
      return types_.voidT();
    }
    if (name == "len") {
      if (!expectArgs(c, 1, name) || !c.args[0]) return err();
      const Type* t = checkExpr(*c.args[0], nullptr);
      if (bad(t)) return err();
      if (t->kind == TypeKind::List || t->kind == TypeKind::Dict || t->kind == TypeKind::Str ||
          t->kind == TypeKind::Tensor) {
        return types_.intT();
      }
      error(c.args[0]->loc, "len no admite " + t->str(), "len acepta List, Dict, str o Tensor");
      return err();
    }
    if (name == "int") {
      if (!expectArgs(c, 1, name) || !c.args[0]) return err();
      const Type* t = checkExpr(*c.args[0], nullptr);
      if (bad(t)) return err();
      if (t == types_.intT() || t == types_.floatT() || t == types_.boolT() || t == types_.strT()) return types_.intT();
      error(c.args[0]->loc, "int() no admite " + t->str(), "int(x) acepta float, int, bool o str");
      return err();
    }
    if (name == "float") {
      if (!expectArgs(c, 1, name) || !c.args[0]) return err();
      const Type* t = checkExpr(*c.args[0], nullptr);
      if (bad(t)) return err();
      if (t == types_.intT() || t == types_.floatT() || t == types_.strT()) return types_.floatT();
      error(c.args[0]->loc, "float() no admite " + t->str(), "float(x) acepta int, float o str");
      return err();
    }
    if (name == "str") {
      if (!expectArgs(c, 1, name) || !c.args[0]) return err();
      const Type* t = checkExpr(*c.args[0], nullptr);
      if (isVoid(t)) error(c.args[0]->loc, "str() no admite valores void", "la función no devuelve valor");
      return types_.strT();
    }
    if (name == "zeros" || name == "ones" || name == "randn") {
      if (c.args.empty()) {
        error(c.loc, "'" + name + "' necesita al menos una dimensión", "escribe " + name + "(2, 3)");
        return err();
      }
      std::vector<int64_t> shape = dimsFrom(c, 0);
      return types_.tensorOf(types_.floatT(), shape);
    }
    if (name == "from_list") return fromList(c);

    checkAll(c);
    return err();
  }
};

}  // namespace

bool analyze(Program& prog, TypeTable& types, Diagnostics& diags) {
  const size_t before = diags.errorCount();
  Sema sema(prog, types, diags);
  sema.run();
  return diags.errorCount() == before;
}

}  // namespace lg
