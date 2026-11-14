// CONTRATO (propietario: núcleo). AST compartido por parser, sema y codegen.
// Reglas:
//  - El parser crea los nodos y deja los campos marcados "sema" sin rellenar.
//  - Sema rellena los campos marcados "sema" y no cambia la estructura del árbol.
//  - Codegen lee el árbol anotado; no vuelve a resolver nombres ni tipos.
//
// Adiciones de Option[T] y clausuras:
//  - TypeExpr::Kind::Option (args[0] = T) y TypeExpr::Kind::Func (args = parámetros, ret = retorno).
//  - ExprKind::NoneLit: `None`. `Some(x)` es un Call con builtin "Some" (CallKind::Builtin).
//    Los métodos de Option (es_some, es_none, desempacar_o) son CallKind::BuiltinMethod con
//    target "Option.es_some", "Option.es_none", "Option.desempacar_o".
//  - StmtKind::Fn (FnStmt): `fn` dentro de un bloque. Sema la declara como variable inmutable del
//    tipo FnDecl::funcType en el ámbito donde aparece y la analiza como función con
//    FnDecl::captures. Una llamada a un valor de tipo función es CallKind::Closure.
//  - VarDecl::captured y Param::captured: `mut` capturada por una función anidada (por referencia).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "common/diagnostics.h"
#include "sema/types.h"

namespace lg {

// ============================ Tipos escritos en el código ============================
struct TypeExpr {
  // Option: Option[T] (valor opcional: Some(x) o None).
  // Func:   fn(A, B) -> R  (tipo de una función anidada usada como valor; sin "-> R" es void).
  // Hilo, Canal, Debil: Hilo[T], Canal[T], Debil[T] (args[0] = T), tipos de concurrencia (v2).
  enum class Kind { Name, List, Dict, Tensor, Option, Func, Hilo, Canal, Debil };
  Kind kind = Kind::Name;
  Loc loc;
  std::string name;                              // Name: "int","float","bool","str" o nombre de struct
  std::vector<std::unique_ptr<TypeExpr>> args;   // List: [T]. Dict: [K, V]. Tensor: [T]. Option: [T]. Func: parámetros
  std::vector<int64_t> dims;                     // Tensor: Tensor[float, (2, 3)] -> {2, 3}
  std::unique_ptr<TypeExpr> ret;                 // Func: tipo de retorno (null = void)
};
using TypeExprPtr = std::unique_ptr<TypeExpr>;

struct Expr;
struct Stmt;
using ExprPtr = std::unique_ptr<Expr>;
using StmtPtr = std::unique_ptr<Stmt>;

// ============================ Expresiones ============================
enum class ExprKind {
  IntLit, FloatLit, BoolLit, StrLit,
  Ident, Unary, Binary, Call, Index, Member,
  ListLit, DictLit, Range,
  NoneLit,  // None: ausencia de valor de un Option[T]; su tipo viene del contexto (sema)
  Await     // await h: espera un Hilo[T] y produce T (AwaitExpr)
};

struct Expr {
  ExprKind kind;
  Loc loc;
  const Type* type = nullptr;  // sema: tipo de la expresión
  Expr(ExprKind k, Loc l) : kind(k), loc(std::move(l)) {}
  virtual ~Expr() = default;
};

struct IntLit : Expr {
  int64_t value = 0;
  IntLit(Loc l, int64_t v) : Expr(ExprKind::IntLit, std::move(l)), value(v) {}
};

struct FloatLit : Expr {
  double value = 0;
  FloatLit(Loc l, double v) : Expr(ExprKind::FloatLit, std::move(l)), value(v) {}
};

struct BoolLit : Expr {
  bool value = false;
  BoolLit(Loc l, bool v) : Expr(ExprKind::BoolLit, std::move(l)), value(v) {}
};

struct StrLit : Expr {
  std::string value;  // sin comillas y con escapes ya procesados
  StrLit(Loc l, std::string v) : Expr(ExprKind::StrLit, std::move(l)), value(std::move(v)) {}
};

struct NoneLit : Expr {
  explicit NoneLit(Loc l) : Expr(ExprKind::NoneLit, std::move(l)) {}
};

struct Ident : Expr {
  std::string name;
  Ident(Loc l, std::string n) : Expr(ExprKind::Ident, std::move(l)), name(std::move(n)) {}
};

// await e: e debe ser Hilo[T]. Equivale a e.esperar() (sin ejecutor: un hilo por llamada).
struct AwaitExpr : Expr {
  ExprPtr operand;
  AwaitExpr(Loc l, ExprPtr e) : Expr(ExprKind::Await, std::move(l)), operand(std::move(e)) {}
};

enum class UnaryOp { Neg, Not };
struct Unary : Expr {
  UnaryOp op;
  ExprPtr operand;
  Unary(Loc l, UnaryOp o, ExprPtr e)
      : Expr(ExprKind::Unary, std::move(l)), op(o), operand(std::move(e)) {}
};

// Precedencia (de menor a mayor): or < and < not < comparaciones (== != < > <= >= in)
//   < + - < * / % @ < unario - < postfijo (llamada, [], .)
enum class BinaryOp { Add, Sub, Mul, Div, Mod, MatMul, Eq, Ne, Lt, Gt, Le, Ge, And, Or, In };
struct Binary : Expr {
  BinaryOp op;
  ExprPtr lhs, rhs;
  Binary(Loc l, BinaryOp o, ExprPtr a, ExprPtr b)
      : Expr(ExprKind::Binary, std::move(l)), op(o), lhs(std::move(a)), rhs(std::move(b)) {}
};

enum class CallKind {
  Unresolved,   // sema aún no ha resuelto
  Spawn,        // hilo f(args) o llamada a una `async fn`: lanza f en un hilo nuevo; devuelve Hilo[T]. target = nombre LLVM
  Function,     // función de usuario: target = nombre LLVM (mangled)
  Extern,       // función extern "C": target = nombre C
  StructCtor,   // Punto(x: 1.0, y: 2.0): target = nombre de la struct
  Method,       // obj.metodo(args): target = nombre LLVM del método; callee es Member
  Builtin,      // print, len, int, float, str, zeros, ones, randn, from_list, Some
  BuiltinMethod, // lista.append(x), tensor.transpose(), dict.tiene(k), opcion.es_some() ... target = "Tipo.método"
  Closure       // f(args) con f de tipo fn(...): callee es cualquier expresión de tipo función (p. ej. Ident)
};
struct Call : Expr {
  ExprPtr callee;                     // Ident (función, struct, builtin) o Member (método)
  std::vector<ExprPtr> args;
  std::vector<std::string> argNames;  // mismo tamaño que args; "" si es posicional
  bool spawn = false;                 // parser: la llamada va precedida de `hilo`
  std::vector<TypeExprPtr> typeArgs;  // parser: Canal[T]() tiene [T]; el resto, vacío
  CallKind callKind = CallKind::Unresolved;  // sema
  std::string target;                 // sema
  Call(Loc l, ExprPtr c) : Expr(ExprKind::Call, std::move(l)), callee(std::move(c)) {}
};

struct Index : Expr {
  ExprPtr object, index;
  // Índices adicionales de t[i, j, ...]: index es el primero y rest los siguientes (vacío si hay uno solo).
  // Solo válido sobre Tensor; el resto de tipos rechaza más de un índice en sema.
  std::vector<ExprPtr> rest;
  Index(Loc l, ExprPtr o, ExprPtr i)
      : Expr(ExprKind::Index, std::move(l)), object(std::move(o)), index(std::move(i)) {}
};

struct Member : Expr {
  ExprPtr object;
  std::string name;
  bool isField = false;  // sema: true = acceso a campo de struct; false = método (dentro de Call)
  Member(Loc l, ExprPtr o, std::string n)
      : Expr(ExprKind::Member, std::move(l)), object(std::move(o)), name(std::move(n)) {}
};

struct ListLit : Expr {
  std::vector<ExprPtr> elems;
  explicit ListLit(Loc l) : Expr(ExprKind::ListLit, std::move(l)) {}
};

struct DictLit : Expr {
  std::vector<std::pair<ExprPtr, ExprPtr>> entries;
  explicit DictLit(Loc l) : Expr(ExprKind::DictLit, std::move(l)) {}
};

// a..b : solo válido como iterable de `for`. Incluye a, excluye b.
struct RangeExpr : Expr {
  ExprPtr start, end;
  RangeExpr(Loc l, ExprPtr s, ExprPtr e)
      : Expr(ExprKind::Range, std::move(l)), start(std::move(s)), end(std::move(e)) {}
};

// ============================ Sentencias ============================
enum class StmtKind { VarDecl, Assign, ExprStmt, Return, If, For, While, Break, Continue, Fn };

struct Stmt {
  StmtKind kind;
  Loc loc;
  Stmt(StmtKind k, Loc l) : kind(k), loc(std::move(l)) {}
  virtual ~Stmt() = default;
};

// `mut x = e`  o  `x: T = e` (con anotación de tipo). Siempre declara.
struct VarDecl : Stmt {
  std::string name;
  bool isMut = false;
  TypeExprPtr annot;   // puede ser null
  ExprPtr init;
  const Type* type = nullptr;  // sema
  bool captured = false;       // sema: una función anidada la captura por referencia (vive en una celda del heap)
  bool isGlobal = false;       // sema: `mut` de nivel superior usada por alguna función: vive como global LLVM
  VarDecl(Loc l) : Stmt(StmtKind::VarDecl, std::move(l)) {}
};

// `x = e` (sin mut ni anotación). Puede declarar o reasignar.
// target: Ident, Index o Member.
struct Assign : Stmt {
  ExprPtr target;
  ExprPtr value;
  bool declares = false;  // sema: true si crea una variable nueva en este punto
  Assign(Loc l) : Stmt(StmtKind::Assign, std::move(l)) {}
};

struct ExprStmt : Stmt {
  ExprPtr expr;
  ExprStmt(Loc l) : Stmt(StmtKind::ExprStmt, std::move(l)) {}
};

struct ReturnStmt : Stmt {
  ExprPtr value;  // null en `return` sin valor
  ReturnStmt(Loc l) : Stmt(StmtKind::Return, std::move(l)) {}
};

// `elif` se desugara en el parser: else: [ If(...) ].
struct IfStmt : Stmt {
  ExprPtr cond;
  std::vector<StmtPtr> thenBody;
  std::vector<StmtPtr> elseBody;  // vacío si no hay else
  IfStmt(Loc l) : Stmt(StmtKind::If, std::move(l)) {}
};

struct ForStmt : Stmt {
  std::string var;
  ExprPtr iter;                   // RangeExpr, o cualquier expresión de tipo List[T]
  std::vector<StmtPtr> body;
  const Type* varType = nullptr;  // sema
  ForStmt(Loc l) : Stmt(StmtKind::For, std::move(l)) {}
};

struct WhileStmt : Stmt {
  ExprPtr cond;
  std::vector<StmtPtr> body;
  WhileStmt(Loc l) : Stmt(StmtKind::While, std::move(l)) {}
};

struct BreakStmt : Stmt { BreakStmt(Loc l) : Stmt(StmtKind::Break, std::move(l)) {} };
struct ContinueStmt : Stmt { ContinueStmt(Loc l) : Stmt(StmtKind::Continue, std::move(l)) {} };

// Sentencia `fn` dentro de un bloque (función anidada o clausura). Solo visible en su ámbito.
struct FnDecl;
struct FnStmt : Stmt {
  std::unique_ptr<FnDecl> fn;
  FnStmt(Loc l) : Stmt(StmtKind::Fn, std::move(l)) {}
};

// ============================ Declaraciones de nivel superior ============================
enum class DeclKind { Fn, Struct, Impl, Import };

struct Decl {
  DeclKind kind;
  Loc loc;
  Decl(DeclKind k, Loc l) : kind(k), loc(std::move(l)) {}
  virtual ~Decl() = default;
};

struct Param {
  std::string name;   // "self" para el receptor de un método
  Loc loc;
  TypeExprPtr annot;  // null para self
  bool isSelf = false;
  bool selfMut = false;        // `mut self`
  const Type* type = nullptr;  // sema
  bool captured = false;       // sema: una función anidada captura este parámetro por referencia (solo `mut self`)
};

// Variable capturada por una función anidada (sema). El valor inicial del campo de la clausura
// es: una copia (si es inmutable) o la celda compartida (si es `mut`, captura por referencia).
struct Capture {
  std::string name;
  const Type* type = nullptr;
  bool byRef = false;  // true si la variable es `mut`
};

struct FnDecl : Decl {
  std::string name;
  std::vector<Param> params;
  TypeExprPtr retAnnot;           // null = sin retorno (void)
  std::vector<StmtPtr> body;      // vacío si !hasBody
  bool isExtern = false;          // extern "C" fn ...
  bool isAsync = false;           // async fn (solo de nivel superior): llamarla devuelve Hilo[T]
  bool hasBody = true;            // extern no tiene cuerpo
  std::string abi;                // "C" para extern, "" para funciones del lenguaje
  const Type* retType = nullptr;  // sema
  std::string mangled;            // sema: nombre LLVM ("u_suma", "u_Punto_norma"; prefijo u_ para no chocar con lg_ del runtime) o nombre C para extern
  std::string implOf;             // sema: struct a la que pertenece si es método
  // Solo funciones anidadas (FnStmt):
  const Type* funcType = nullptr;  // sema: tipo fn(params) -> ret del valor de la función
  std::vector<Capture> captures;   // sema: variables del ámbito externo que usa (en orden de primera aparición)
  FnDecl(Loc l) : Decl(DeclKind::Fn, std::move(l)) {}
};

struct FieldDecl {
  std::string name;
  Loc loc;
  TypeExprPtr annot;
  const Type* type = nullptr;  // sema
};

struct StructDecl : Decl {
  std::string name;
  std::vector<FieldDecl> fields;  // el orden es el orden de declaración (orden en memoria)
  StructDecl(Loc l) : Decl(DeclKind::Struct, std::move(l)) {}
};

struct ImplDecl : Decl {
  std::string typeName;
  Loc typeLoc;
  std::vector<std::unique_ptr<FnDecl>> methods;
  ImplDecl(Loc l) : Decl(DeclKind::Impl, std::move(l)) {}
};

// `import util` carga util.lg del mismo directorio. Sus declaraciones quedan visibles
// en el ámbito global (importación plana). Un archivo se carga una sola vez.
struct ImportDecl : Decl {
  std::string path;  // "util" o "carpeta/util"
  ImportDecl(Loc l) : Decl(DeclKind::Import, std::move(l)) {}
};

struct Program {
  std::string file;                              // archivo principal
  std::vector<std::unique_ptr<Decl>> decls;      // fn, struct, impl, import (tras la carga, los imports quedan)
  std::vector<StmtPtr> stmts;                    // sentencias de nivel superior, en orden.
                                                 // Se ejecutan en lg_main, tras las de los módulos importados.
};

}  // namespace lg
