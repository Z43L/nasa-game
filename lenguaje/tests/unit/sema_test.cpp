// Pruebas unitarias de sema: los ASTs se construyen a mano (no dependen del parser).
#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ast/ast.h"
#include "common/diagnostics.h"
#include "parser/parser.h"
#include "sema/sema.h"
#include "sema/types.h"

using namespace lg;

namespace {

Loc L(int line = 1) { return Loc{"test.lg", line, 1}; }

// ---------- expresiones ----------
ExprPtr EInt(int64_t v) { return std::make_unique<IntLit>(L(), v); }
ExprPtr EFlt(double v) { return std::make_unique<FloatLit>(L(), v); }
ExprPtr EStr(std::string v) { return std::make_unique<StrLit>(L(), std::move(v)); }
ExprPtr EBool(bool v) { return std::make_unique<BoolLit>(L(), v); }
ExprPtr EId(std::string n) { return std::make_unique<Ident>(L(), std::move(n)); }
ExprPtr EBin(BinaryOp op, ExprPtr a, ExprPtr b) { return std::make_unique<Binary>(L(), op, std::move(a), std::move(b)); }
ExprPtr EMem(ExprPtr o, std::string n) { return std::make_unique<Member>(L(), std::move(o), std::move(n)); }

ExprPtr EList(std::vector<ExprPtr> els) {
  auto l = std::make_unique<ListLit>(L());
  l->elems = std::move(els);
  return ExprPtr(std::move(l));
}

template <typename... Ts>
std::vector<ExprPtr> Args(Ts&&... ts) {
  std::vector<ExprPtr> v;
  (v.push_back(ExprPtr(std::move(ts))), ...);
  return v;
}

// Llamada a función/builtin/struct por nombre. names: argumentos con nombre (vacío = posicional).
ExprPtr ECall(const std::string& fn, std::vector<ExprPtr> args, std::vector<std::string> names = {}) {
  auto c = std::make_unique<Call>(L(), EId(fn));
  c->args = std::move(args);
  c->argNames = std::move(names);
  c->argNames.resize(c->args.size());
  return ExprPtr(std::move(c));
}

// Llamada a método: obj.metodo(args)
ExprPtr EMCall(ExprPtr obj, const std::string& m, std::vector<ExprPtr> args = {}) {
  auto c = std::make_unique<Call>(L(), EMem(std::move(obj), m));
  c->args = std::move(args);
  c->argNames.resize(c->args.size());
  return ExprPtr(std::move(c));
}

// ---------- sentencias ----------
StmtPtr SVar(const std::string& name, bool isMut, ExprPtr init, TypeExprPtr annot = nullptr) {
  auto v = std::make_unique<VarDecl>(L());
  v->name = name;
  v->isMut = isMut;
  v->init = std::move(init);
  v->annot = std::move(annot);
  return StmtPtr(std::move(v));
}

StmtPtr SAsg(const std::string& name, ExprPtr value) {
  auto a = std::make_unique<Assign>(L());
  a->target = EId(name);
  a->value = std::move(value);
  return StmtPtr(std::move(a));
}

StmtPtr SAsgTo(ExprPtr target, ExprPtr value) {
  auto a = std::make_unique<Assign>(L());
  a->target = std::move(target);
  a->value = std::move(value);
  return StmtPtr(std::move(a));
}

StmtPtr SExpr(ExprPtr e) {
  auto s = std::make_unique<ExprStmt>(L());
  s->expr = std::move(e);
  return StmtPtr(std::move(s));
}

StmtPtr SRet(ExprPtr e = nullptr) {
  auto r = std::make_unique<ReturnStmt>(L());
  r->value = std::move(e);
  return StmtPtr(std::move(r));
}

StmtPtr SIf(ExprPtr cond, std::vector<StmtPtr> thenB, std::vector<StmtPtr> elseB = {}) {
  auto i = std::make_unique<IfStmt>(L());
  i->cond = std::move(cond);
  i->thenBody = std::move(thenB);
  i->elseBody = std::move(elseB);
  return StmtPtr(std::move(i));
}

StmtPtr SFor(const std::string& var, ExprPtr iter, std::vector<StmtPtr> body) {
  auto f = std::make_unique<ForStmt>(L());
  f->var = var;
  f->iter = std::move(iter);
  f->body = std::move(body);
  return StmtPtr(std::move(f));
}

StmtPtr SBreak() { return StmtPtr(std::make_unique<BreakStmt>(L())); }

// ---------- tipos escritos ----------
TypeExprPtr TName(const std::string& n) {
  auto t = std::make_unique<TypeExpr>();
  t->kind = TypeExpr::Kind::Name;
  t->name = n;
  return t;
}

TypeExprPtr TList(TypeExprPtr elem) {
  auto t = std::make_unique<TypeExpr>();
  t->kind = TypeExpr::Kind::List;
  t->args.push_back(std::move(elem));
  return t;
}

TypeExprPtr TTensor(const std::string& elem, std::vector<int64_t> dims) {
  auto t = std::make_unique<TypeExpr>();
  t->kind = TypeExpr::Kind::Tensor;
  t->args.push_back(TName(elem));
  t->dims = std::move(dims);
  return t;
}

// ---------- declaraciones ----------
Param PParam(const std::string& name, TypeExprPtr annot) {
  Param p;
  p.name = name;
  p.loc = L();
  p.annot = std::move(annot);
  return p;
}

Param PSelf(bool selfMut) {
  Param p;
  p.name = "self";
  p.loc = L();
  p.isSelf = true;
  p.selfMut = selfMut;
  return p;
}

template <typename... T>
std::vector<Param> Ps(T&&... t) {
  std::vector<Param> v;
  (v.push_back(std::move(t)), ...);
  return v;
}

std::unique_ptr<FnDecl> MkFn(const std::string& name, std::vector<Param> params, TypeExprPtr ret,
                             std::vector<StmtPtr> body) {
  auto f = std::make_unique<FnDecl>(L());
  f->name = name;
  f->params = std::move(params);
  f->retAnnot = std::move(ret);
  f->body = std::move(body);
  return f;
}

std::unique_ptr<StructDecl> MkStruct(const std::string& name,
                                     const std::vector<std::pair<std::string, std::string>>& fields) {
  auto s = std::make_unique<StructDecl>(L());
  s->name = name;
  for (const auto& [fname, ftype] : fields) {
    FieldDecl f;
    f.name = fname;
    f.loc = L();
    f.annot = TName(ftype);
    s->fields.push_back(std::move(f));
  }
  return s;
}

// ---------- ejecución y diagnósticos ----------
struct SemaRun {
  TypeTable types;
  Diagnostics diags;
  bool ok = false;

  bool hasMsg(const std::string& frag) const {
    for (const auto& d : diags.all()) {
      if (d.message.find(frag) != std::string::npos) return true;
    }
    return false;
  }
  bool hasHint(const std::string& frag) const {
    for (const auto& d : diags.all()) {
      if (d.hint.find(frag) != std::string::npos) return true;
    }
    return false;
  }
};

void Analyze(Program& prog, SemaRun& r) { r.ok = analyze(prog, r.types, r.diags); }

const Assign& AsAssign(const Program& p, size_t i) { return static_cast<const Assign&>(*p.stmts[i]); }

}  // namespace

// ============================ inmutabilidad ============================

TEST(SemaImmutability, ReassignWithoutMutIsError) {
  Program prog;
  prog.stmts.push_back(SVar("x", false, EInt(1)));
  prog.stmts.push_back(SAsg("x", EInt(2)));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("variable inmutable 'x'"));
  EXPECT_TRUE(r.hasHint("mut x"));
}

TEST(SemaImmutability, ReassignWithMutIsOk) {
  Program prog;
  prog.stmts.push_back(SVar("x", true, EInt(1)));
  prog.stmts.push_back(SAsg("x", EInt(2)));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok) << "errores inesperados";
  EXPECT_FALSE(AsAssign(prog, 1).declares);
}

TEST(SemaImmutability, FieldAssignNeedsMutBase) {
  Program prog;
  prog.decls.push_back(MkStruct("Punto", {{"x", "float"}}));
  prog.stmts.push_back(SVar("p", false, ECall("Punto", Args(EFlt(1.0)), {"x"})));
  prog.stmts.push_back(SAsgTo(EMem(EId("p"), "x"), EFlt(2.0)));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("variable inmutable 'p'"));
}

// ============================ inferencia y declaraciones ============================

TEST(SemaInference, AssignDeclaresWithInferredType) {
  Program prog;
  prog.stmts.push_back(SAsg("y", EInt(10)));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
  const auto& a = AsAssign(prog, 0);
  EXPECT_TRUE(a.declares);
  EXPECT_EQ(a.target->type, r.types.intT());
}

TEST(SemaInference, DeclaredVarHasInitType) {
  Program prog;
  prog.stmts.push_back(SVar("s", false, EStr("hola")));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(static_cast<const VarDecl&>(*prog.stmts[0]).type, r.types.strT());
}

TEST(SemaInference, EmptyListWithoutAnnotationIsError) {
  Program prog;
  prog.stmts.push_back(SAsg("x", EList({})));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("lista vacía"));
  EXPECT_TRUE(r.hasHint("List[int]"));
}

TEST(SemaInference, EmptyListWithAnnotationIsOk) {
  Program prog;
  prog.stmts.push_back(SVar("x", true, EList({}), TList(TName("int"))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
}

// ============================ mismatch de tipos ============================

TEST(SemaTypes, MismatchIntStr) {
  Program prog;
  prog.stmts.push_back(SVar("a", true, EInt(1)));
  prog.stmts.push_back(SAsg("a", EStr("hola")));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("tipo esperado int, recibido str"));
}

TEST(SemaTypes, MixedIntFloatIsErrorWithHint) {
  Program prog;
  prog.stmts.push_back(SAsg("a", EBin(BinaryOp::Add, EInt(1), EFlt(2.0))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("no se mezclan int y float"));
  EXPECT_TRUE(r.hasHint("float(x)"));
}

TEST(SemaTypes, IntDivisionYieldsInt) {
  Program prog;
  prog.stmts.push_back(SAsg("q", EBin(BinaryOp::Div, EInt(7), EInt(2))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(AsAssign(prog, 0).target->type, r.types.intT());
}

TEST(SemaTypes, ModRequiresInt) {
  Program prog;
  prog.stmts.push_back(SAsg("q", EBin(BinaryOp::Mod, EFlt(7.0), EFlt(2.0))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
}

TEST(SemaTypes, FunctionParamMismatch) {
  Program prog;
  prog.decls.push_back(MkFn("doble", Ps(PParam("x", TName("int"))), TName("int"),
                            [] {
                              std::vector<StmtPtr> b;
                              b.push_back(SRet(EBin(BinaryOp::Mul, EId("x"), EInt(2))));
                              return b;
                            }()));
  prog.stmts.push_back(SExpr(ECall("doble", Args(EStr("no")))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("tipo esperado int, recibido str"));
}

TEST(SemaTypes, ParamWithoutTypeIsError) {
  Program prog;
  prog.decls.push_back(MkFn("f", Ps(PParam("x", nullptr)), nullptr, {}));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("falta el tipo del parámetro 'x'"));
}

TEST(SemaTypes, ReturnMissingValueIsError) {
  Program prog;
  std::vector<StmtPtr> body;
  body.push_back(SRet());
  prog.decls.push_back(MkFn("f", {}, TName("int"), std::move(body)));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("falta el valor de retorno"));
}

TEST(SemaTypes, NonVoidFunctionMustReturn) {
  Program prog;
  prog.decls.push_back(MkFn("f", {}, TName("int"), {}));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("no devuelve un valor en todos los caminos"));
}

// ============================ tensores ============================

TEST(SemaTensor, IncompatibleDeclaredShapeIsCompileError) {
  Program prog;
  prog.stmts.push_back(SVar("a", false, ECall("zeros", Args(EInt(3), EInt(2))), TTensor("float", {2, 3})));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("forma de tensor incompatible"));
}

TEST(SemaTensor, CompatibleDeclaredShapeIsOk) {
  Program prog;
  prog.stmts.push_back(SVar("a", false, ECall("zeros", Args(EInt(2), EInt(3))), TTensor("float", {2, 3})));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(static_cast<const VarDecl&>(*prog.stmts[0]).type, r.types.tensorOf(r.types.floatT(), {2, 3}));
}

TEST(SemaTensor, NonLiteralDimIsUnknownAndCompatible) {
  Program prog;
  prog.stmts.push_back(SVar("n", true, EInt(2)));
  prog.stmts.push_back(SVar("a", false, ECall("zeros", Args(EId("n"), EInt(3))), TTensor("float", {2, 3})));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
}

TEST(SemaTensor, MatMulKnownShapesMismatchIsCompileError) {
  Program prog;
  prog.stmts.push_back(SVar("a", false, ECall("zeros", Args(EInt(2), EInt(3)))));
  prog.stmts.push_back(SAsg("b", EBin(BinaryOp::MatMul, EId("a"), ECall("zeros", Args(EInt(2), EInt(3))))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("formas incompatibles para '@'"));
}

TEST(SemaTensor, MatMulResultShape) {
  Program prog;
  prog.stmts.push_back(SVar("a", false, ECall("zeros", Args(EInt(2), EInt(3)))));
  prog.stmts.push_back(SAsg("c", EBin(BinaryOp::MatMul, EId("a"), ECall("zeros", Args(EInt(3), EInt(4))))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(AsAssign(prog, 1).target->type, r.types.tensorOf(r.types.floatT(), {2, 4}));
}

TEST(SemaTensor, FromListCountMismatchIsError) {
  Program prog;
  prog.stmts.push_back(SAsg("t", ECall("from_list", Args(EList(Args(EFlt(1.0), EFlt(2.0), EFlt(3.0))), EInt(2), EInt(2)))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("from_list"));
}

TEST(SemaTensor, TransposeInvertsShape) {
  Program prog;
  prog.stmts.push_back(SVar("a", false, ECall("zeros", Args(EInt(2), EInt(5)))));
  prog.stmts.push_back(SAsg("t", EMCall(EId("a"), "transpose")));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
  const Call& c = static_cast<const Call&>(*AsAssign(prog, 1).value);
  EXPECT_EQ(c.callKind, CallKind::BuiltinMethod);
  EXPECT_EQ(c.target, "Tensor.transpose");
  EXPECT_EQ(c.type, r.types.tensorOf(r.types.floatT(), {5, 2}));
}

// ============================ structs ============================

TEST(SemaStruct, CtorWithAllFieldsIsOk) {
  Program prog;
  prog.decls.push_back(MkStruct("Punto", {{"x", "float"}, {"y", "float"}}));
  prog.stmts.push_back(SAsg("p", ECall("Punto", Args(EFlt(1.0), EFlt(2.0)), {"x", "y"})));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
  const Call& c = static_cast<const Call&>(*AsAssign(prog, 0).value);
  EXPECT_EQ(c.callKind, CallKind::StructCtor);
  EXPECT_EQ(c.target, "Punto");
  EXPECT_EQ(c.type, r.types.structOf("Punto"));
}

TEST(SemaStruct, CtorMissingFieldIsError) {
  Program prog;
  prog.decls.push_back(MkStruct("Punto", {{"x", "float"}, {"y", "float"}}));
  prog.stmts.push_back(SAsg("p", ECall("Punto", Args(EFlt(1.0)), {"x"})));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("faltan campos en 'Punto': y"));
}

TEST(SemaStruct, CtorWrongFieldTypeIsError) {
  Program prog;
  prog.decls.push_back(MkStruct("Punto", {{"x", "float"}}));
  prog.stmts.push_back(SAsg("p", ECall("Punto", Args(EInt(1)), {"x"})));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("tipo esperado float, recibido int"));
}

TEST(SemaStruct, FieldAccessIsField) {
  Program prog;
  prog.decls.push_back(MkStruct("Punto", {{"x", "float"}}));
  prog.stmts.push_back(SVar("p", false, ECall("Punto", Args(EFlt(1.0)), {"x"})));
  prog.stmts.push_back(SAsg("v", EMem(EId("p"), "x")));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
  const auto& m = static_cast<const Member&>(*AsAssign(prog, 1).value);
  EXPECT_TRUE(m.isField);
  EXPECT_EQ(m.type, r.types.floatT());
}

// ============================ métodos y self ============================

namespace {

// struct Punto { x: float } con impl: fn getx(self) -> float, fn mover(SELF, dx) -> float
Program MakePuntoProgram(bool selfMut) {
  Program prog;
  prog.decls.push_back(MkStruct("Punto", {{"x", "float"}}));

  auto impl = std::make_unique<ImplDecl>(L());
  impl->typeName = "Punto";
  impl->typeLoc = L();

  {
    std::vector<StmtPtr> b;
    b.push_back(SRet(EMem(EId("self"), "x")));
    impl->methods.push_back(MkFn("getx", Ps(PSelf(false)), TName("float"), std::move(b)));
  }
  {
    std::vector<StmtPtr> b;
    b.push_back(SAsgTo(EMem(EId("self"), "x"), EId("dx")));
    b.push_back(SRet(EMem(EId("self"), "x")));
    std::vector<Param> ps;
    ps.push_back(PSelf(selfMut));
    ps.push_back(PParam("dx", TName("float")));
    impl->methods.push_back(MkFn("mover", std::move(ps), TName("float"), std::move(b)));
  }
  prog.decls.push_back(std::move(impl));
  return prog;
}

}  // namespace

TEST(SemaMethod, SelfFieldAssignNeedsMutSelf) {
  Program prog = MakePuntoProgram(false);
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("variable inmutable 'self'"));
  EXPECT_TRUE(r.hasHint("mut self"));
}

TEST(SemaMethod, MutSelfAllowsFieldAssign) {
  Program prog = MakePuntoProgram(true);
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
}

TEST(SemaMethod, MethodCallResolvesToMangledName) {
  Program prog = MakePuntoProgram(true);
  prog.stmts.push_back(SVar("p", false, ECall("Punto", Args(EFlt(1.0)), {"x"})));
  prog.stmts.push_back(SAsg("r", EMCall(EId("p"), "getx")));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
  const Call& c = static_cast<const Call&>(*AsAssign(prog, 1).value);
  EXPECT_EQ(c.callKind, CallKind::Method);
  EXPECT_EQ(c.target, "u_Punto_getx");
  EXPECT_EQ(c.type, r.types.floatT());
}

TEST(SemaMethod, MutMethodOnImmutableVarIsError) {
  Program prog = MakePuntoProgram(true);
  prog.stmts.push_back(SVar("p", false, ECall("Punto", Args(EFlt(1.0)), {"x"})));
  prog.stmts.push_back(SExpr(EMCall(EId("p"), "mover", Args(EFlt(2.0)))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("inmutable"));
}

TEST(SemaMethod, MethodArgCountIsChecked) {
  Program prog = MakePuntoProgram(false);
  prog.stmts.push_back(SVar("p", false, ECall("Punto", Args(EFlt(1.0)), {"x"})));
  prog.stmts.push_back(SExpr(EMCall(EId("p"), "getx", Args(EFlt(1.0)))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
}

TEST(SemaMethod, SelfMustBeFirstParam) {
  Program prog;
  prog.decls.push_back(MkStruct("Punto", {{"x", "float"}}));
  auto impl = std::make_unique<ImplDecl>(L());
  impl->typeName = "Punto";
  impl->typeLoc = L();
  std::vector<Param> ps;
  ps.push_back(PParam("k", TName("int")));
  ps.push_back(PSelf(false));
  impl->methods.push_back(MkFn("m", std::move(ps), nullptr, {}));
  prog.decls.push_back(std::move(impl));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("'self' debe ser el primer parámetro"));
}

// ============================ builtins ============================

TEST(SemaBuiltins, LenAndConversionsTypes) {
  Program prog;
  prog.stmts.push_back(SVar("lista", true, EList(Args(EInt(1), EInt(2), EInt(3)))));
  prog.stmts.push_back(SAsg("n", ECall("len", Args(EId("lista")))));
  prog.stmts.push_back(SAsg("i", ECall("int", Args(EStr("3")))));
  prog.stmts.push_back(SAsg("f", ECall("float", Args(EInt(1)))));
  prog.stmts.push_back(SAsg("s", ECall("str", Args(EFlt(1.5)))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(AsAssign(prog, 1).target->type, r.types.intT());
  EXPECT_EQ(AsAssign(prog, 2).target->type, r.types.intT());
  EXPECT_EQ(AsAssign(prog, 3).target->type, r.types.floatT());
  EXPECT_EQ(AsAssign(prog, 4).target->type, r.types.strT());
  const Call& c = static_cast<const Call&>(*AsAssign(prog, 1).value);
  EXPECT_EQ(c.callKind, CallKind::Builtin);
  EXPECT_EQ(c.target, "len");
}

TEST(SemaBuiltins, PrintAcceptsAnyArgsAndIsVoid) {
  Program prog;
  prog.stmts.push_back(SExpr(ECall("print", Args(EInt(1), EStr("a"), EBool(true)))));
  prog.stmts.push_back(SExpr(ECall("print", {})));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
}

TEST(SemaBuiltins, LenOfIntIsError) {
  Program prog;
  prog.stmts.push_back(SExpr(ECall("len", Args(EInt(1)))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
}

TEST(SemaBuiltins, ZerosWithLiteralAndUnknownDims) {
  Program prog;
  prog.stmts.push_back(SVar("n", true, EInt(4)));
  prog.stmts.push_back(SAsg("z", ECall("zeros", Args(EInt(2), EId("n")))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(AsAssign(prog, 1).target->type, r.types.tensorOf(r.types.floatT(), {2, -1}));
}

TEST(SemaBuiltins, FromListTypeFollowsElement) {
  Program prog;
  prog.stmts.push_back(SAsg("t", ECall("from_list", Args(EList(Args(EInt(1), EInt(2))), EInt(2)))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(AsAssign(prog, 0).target->type, r.types.tensorOf(r.types.intT(), {2}));
}

TEST(SemaBuiltins, ListAppendRequiresMutList) {
  Program prog;
  prog.stmts.push_back(SVar("lst", false, EList(Args(EInt(1)))));
  prog.stmts.push_back(SExpr(EMCall(EId("lst"), "append", Args(EInt(2)))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("inmutable"));
}

TEST(SemaBuiltins, ListAppendWithMutIsOk) {
  Program prog;
  prog.stmts.push_back(SVar("lst", true, EList(Args(EInt(1)))));
  prog.stmts.push_back(SExpr(EMCall(EId("lst"), "append", Args(EInt(2)))));
  prog.stmts.push_back(SAsg("x", EMCall(EId("lst"), "pop")));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(AsAssign(prog, 2).target->type, r.types.intT());
}

TEST(SemaBuiltins, DictTieneAndLen) {
  Program prog;
  auto dl = std::make_unique<DictLit>(L());
  dl->entries.emplace_back(EStr("a"), EInt(1));
  prog.stmts.push_back(SVar("d", true, ExprPtr(std::move(dl))));
  prog.stmts.push_back(SAsg("b", EMCall(EId("d"), "tiene", Args(EStr("a")))));
  prog.stmts.push_back(SAsg("n", EMCall(EId("d"), "len")));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(AsAssign(prog, 1).target->type, r.types.boolT());
  EXPECT_EQ(AsAssign(prog, 2).target->type, r.types.intT());
}

// ============================ ámbitos y funciones ============================

TEST(SemaScopes, FunctionCanReadMutGlobalVariable) {
  Program prog;
  prog.stmts.push_back(SVar("g", true, EInt(1)));
  std::vector<StmtPtr> b;
  b.push_back(SRet(EId("g")));
  prog.decls.push_back(MkFn("f", {}, TName("int"), std::move(b)));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
  auto* decl = static_cast<VarDecl*>(prog.stmts[0].get());
  EXPECT_TRUE(decl->isGlobal);  // la usa una función: pasa a ser global LLVM
}

TEST(SemaScopes, FunctionCannotReadImmutableGlobalVariable) {
  Program prog;
  prog.stmts.push_back(SVar("g", false, EInt(1)));
  std::vector<StmtPtr> b;
  b.push_back(SRet(EId("g")));
  prog.decls.push_back(MkFn("f", {}, TName("int"), std::move(b)));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("variable global 'g'"));
}

TEST(SemaScopes, AssignInFunctionDoesNotSeeGlobal) {
  Program prog;
  prog.stmts.push_back(SVar("g", false, EInt(1)));
  std::vector<StmtPtr> b;
  b.push_back(SAsg("g", EInt(2)));  // declara una local nueva, no reasigna la global
  b.push_back(SRet(EId("g")));
  prog.decls.push_back(MkFn("f", {}, TName("int"), std::move(b)));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
  const auto& fd = static_cast<const FnDecl&>(*prog.decls[0]);
  EXPECT_TRUE(static_cast<const Assign&>(*fd.body[0]).declares);
}

TEST(SemaScopes, ForwardCallsAreResolved) {
  Program prog;
  {
    std::vector<StmtPtr> b;
    b.push_back(SRet(ECall("b", {})));
    prog.decls.push_back(MkFn("a", {}, TName("int"), std::move(b)));
  }
  {
    std::vector<StmtPtr> b;
    b.push_back(SRet(EInt(1)));
    prog.decls.push_back(MkFn("b", {}, TName("int"), std::move(b)));
  }
  prog.stmts.push_back(SAsg("x", ECall("a", {})));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
  const Call& c = static_cast<const Call&>(*AsAssign(prog, 0).value);
  EXPECT_EQ(c.callKind, CallKind::Function);
  EXPECT_EQ(c.target, "u_a");
  EXPECT_EQ(static_cast<const FnDecl&>(*prog.decls[0]).mangled, "u_a");
}

TEST(SemaScopes, DuplicateFunctionIsError) {
  Program prog;
  prog.decls.push_back(MkFn("f", {}, nullptr, {}));
  prog.decls.push_back(MkFn("f", {}, nullptr, {}));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("'f' ya está definida"));
}

TEST(SemaScopes, BreakOutsideLoopIsError) {
  Program prog;
  prog.stmts.push_back(SBreak());
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
}

TEST(SemaScopes, ForRangeVarIsInt) {
  Program prog;
  auto rng = std::make_unique<RangeExpr>(L(), EInt(0), EInt(3));
  std::vector<StmtPtr> body;
  body.push_back(SExpr(ECall("print", Args(EId("i")))));
  prog.stmts.push_back(SFor("i", ExprPtr(std::move(rng)), std::move(body)));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(static_cast<const ForStmt&>(*prog.stmts[0]).varType, r.types.intT());
}

TEST(SemaScopes, ForOverListGivesElementType) {
  Program prog;
  prog.stmts.push_back(SVar("lst", false, EList(Args(EStr("a")))));
  std::vector<StmtPtr> body;
  body.push_back(SExpr(ECall("print", Args(EId("x")))));
  prog.stmts.push_back(SFor("x", EId("lst"), std::move(body)));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(static_cast<const ForStmt&>(*prog.stmts[1]).varType, r.types.strT());
}

TEST(SemaScopes, IfConditionMustBeBool) {
  Program prog;
  std::vector<StmtPtr> body;
  prog.stmts.push_back(SIf(EInt(1), std::move(body)));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("debe ser bool"));
}

TEST(SemaScopes, ErrorRecoveryDoesNotCascade) {
  Program prog;
  prog.stmts.push_back(SAsg("a", EId("no_existe")));
  prog.stmts.push_back(SAsg("b", EBin(BinaryOp::Add, EId("a"), EInt(1))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  // Solo el error original; "a" tiene tipo error y no genera más mensajes.
  EXPECT_EQ(r.diags.errorCount(), 1u);
}

TEST(SemaScopes, ReservedFunctionNameIsError) {
  Program prog;
  prog.decls.push_back(MkFn("print", {}, nullptr, {}));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
}

// ---------- tensores: indexación y operaciones con escalar ----------
ExprPtr EIdx(ExprPtr o, ExprPtr i) { return std::make_unique<Index>(L(), std::move(o), std::move(i)); }
ExprPtr EUnNeg(ExprPtr e) { return std::make_unique<Unary>(L(), UnaryOp::Neg, std::move(e)); }

// Índice múltiple t[i, j, ...]: el primero va en Index::index y el resto en Index::rest.
ExprPtr EIdxN(ExprPtr o, std::vector<ExprPtr> idx) {
  auto ix = std::make_unique<Index>(L(), std::move(o), std::move(idx[0]));
  for (size_t k = 1; k < idx.size(); k++) ix->rest.push_back(std::move(idx[k]));
  return ix;
}

// let t: Tensor[float, (2, 3)] = zeros(2, 3)
StmtPtr STensor2x3(bool isMut = false) {
  return SVar("t", isMut, ECall("zeros", Args(EInt(2), EInt(3))));
}

TEST(SemaTensorIndex, FullIndexYieldsElementType) {
  Program prog;
  prog.stmts.push_back(STensor2x3());
  prog.stmts.push_back(SVar("a", false, EIdxN(EId("t"), Args(EInt(1), EInt(2)))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(static_cast<const VarDecl&>(*prog.stmts[1]).type, r.types.floatT());
}

TEST(SemaTensorIndex, PartialIndexYieldsSubTensor) {
  Program prog;
  prog.stmts.push_back(STensor2x3());
  prog.stmts.push_back(SVar("a", false, EIdx(EId("t"), EInt(1))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(static_cast<const VarDecl&>(*prog.stmts[1]).type, r.types.tensorOf(r.types.floatT(), {3}));
}

TEST(SemaTensorIndex, NegativeLiteralInRangeIsOk) {
  Program prog;
  prog.stmts.push_back(STensor2x3());
  prog.stmts.push_back(SVar("a", false, EIdxN(EId("t"), Args(EUnNeg(EInt(2)), EUnNeg(EInt(3))))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
}

TEST(SemaTensorIndex, LiteralOutOfRangeIsCompileError) {
  Program prog;
  prog.stmts.push_back(STensor2x3());
  prog.stmts.push_back(SVar("a", false, EIdxN(EId("t"), Args(EInt(2), EInt(0)))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("índice 2 fuera de rango (dimensión 0 de tamaño 2)"));
}

TEST(SemaTensorIndex, TooManyIndicesIsError) {
  Program prog;
  prog.stmts.push_back(STensor2x3());
  prog.stmts.push_back(SVar("a", false, EIdxN(EId("t"), Args(EInt(0), EInt(0), EInt(0)))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("demasiados índices"));
}

TEST(SemaTensorIndex, FloatIndexIsError) {
  Program prog;
  prog.stmts.push_back(STensor2x3());
  prog.stmts.push_back(SVar("a", false, EIdx(EId("t"), EFlt(1.0))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
}

TEST(SemaTensorIndex, AssignWithoutMutIsError) {
  Program prog;
  prog.stmts.push_back(STensor2x3());
  prog.stmts.push_back(SAsgTo(EIdxN(EId("t"), Args(EInt(0), EInt(1))), EFlt(1.0)));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("variable inmutable 't'"));
}

TEST(SemaTensorIndex, AssignWithMutIsOk) {
  Program prog;
  prog.stmts.push_back(STensor2x3(true));
  prog.stmts.push_back(SAsgTo(EIdxN(EId("t"), Args(EInt(0), EInt(1))), EFlt(1.0)));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
}

TEST(SemaTensorIndex, AssignElementTypeMustMatch) {
  Program prog;
  prog.stmts.push_back(STensor2x3(true));
  prog.stmts.push_back(SAsgTo(EIdxN(EId("t"), Args(EInt(0), EInt(1))), EInt(1)));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
}

TEST(SemaTensorIndex, PartialAssignIsError) {
  Program prog;
  prog.stmts.push_back(STensor2x3(true));
  prog.stmts.push_back(SAsgTo(EIdx(EId("t"), EInt(0)), EFlt(1.0)));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("tensor parcial"));
}

TEST(SemaTensorScalar, ScalarTimesTensorIsTensor) {
  Program prog;
  prog.stmts.push_back(STensor2x3());
  prog.stmts.push_back(SVar("a", false, EBin(BinaryOp::Mul, EFlt(2.0), EId("t"))));
  prog.stmts.push_back(SVar("b", false, EBin(BinaryOp::Div, EId("t"), EFlt(2.0))));
  prog.stmts.push_back(SVar("c", false, EBin(BinaryOp::Sub, EFlt(1.0), EId("t"))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
  const Type* tt = r.types.tensorOf(r.types.floatT(), {2, 3});
  EXPECT_EQ(static_cast<const VarDecl&>(*prog.stmts[1]).type, tt);
  EXPECT_EQ(static_cast<const VarDecl&>(*prog.stmts[2]).type, tt);
  EXPECT_EQ(static_cast<const VarDecl&>(*prog.stmts[3]).type, tt);
}

TEST(SemaTensorScalar, IntScalarWithIntTensorIsTensor) {
  Program prog;
  prog.stmts.push_back(SVar("v", false, ECall("from_list", Args(EList(Args(EInt(1), EInt(2), EInt(3))), EInt(3)))));
  prog.stmts.push_back(SVar("a", false, EBin(BinaryOp::Add, EId("v"), EInt(1))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_TRUE(r.ok);
  EXPECT_EQ(static_cast<const VarDecl&>(*prog.stmts[1]).type, r.types.tensorOf(r.types.intT(), {3}));
}

TEST(SemaTensorScalar, FloatScalarWithIntTensorIsErrorWithHint) {
  Program prog;
  prog.stmts.push_back(SVar("v", false, ECall("from_list", Args(EList(Args(EInt(1), EInt(2), EInt(3))), EInt(3)))));
  prog.stmts.push_back(SVar("a", false, EBin(BinaryOp::Mul, EId("v"), EFlt(2.5))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("no se mezclan float y Tensor[int"));
  EXPECT_TRUE(r.hasHint("usa float(x) o un tensor de float"));
}

TEST(SemaTensorScalar, IntScalarWithFloatTensorIsError) {
  Program prog;
  prog.stmts.push_back(STensor2x3());
  prog.stmts.push_back(SVar("a", false, EBin(BinaryOp::Sub, EInt(1), EId("t"))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
}

TEST(SemaTensorScalar, MatMulWithScalarIsError) {
  Program prog;
  prog.stmts.push_back(STensor2x3());
  prog.stmts.push_back(SVar("a", false, EBin(BinaryOp::MatMul, EId("t"), EFlt(2.0))));
  SemaRun r;
  Analyze(prog, r);
  EXPECT_FALSE(r.ok);
}

// ============================ Option[T] y clausuras (desde código fuente) ============================

namespace {

// Analiza un programa escrito en lg: el parser se usa tal cual para cubrir la sintaxis nueva.
struct SrcRun {
  TypeTable types;
  Diagnostics diags;
  std::unique_ptr<Program> prog;
  bool ok = false;

  bool hasMsg(const std::string& frag) const {
    for (const auto& d : diags.all()) {
      if (d.message.find(frag) != std::string::npos) return true;
    }
    return false;
  }
  bool hasHint(const std::string& frag) const {
    for (const auto& d : diags.all()) {
      if (d.hint.find(frag) != std::string::npos) return true;
    }
    return false;
  }
};

void AnalyzeSrc(const std::string& src, SrcRun& r) {
  r.prog = parseProgram("t.lg", src, r.diags);
  r.ok = r.diags.errorCount() == 0 && analyze(*r.prog, r.types, r.diags);
}

// Tipo de la expresión `expr` declarada como `v` dentro de la función `f` del programa.
const Type* TypeOfLocal(const Program& p, const std::string& fname, const std::string& var) {
  for (const auto& d : p.decls) {
    if (d->kind != DeclKind::Fn || static_cast<const FnDecl&>(*d).name != fname) continue;
    for (const auto& s : static_cast<const FnDecl&>(*d).body) {
      if (s->kind == StmtKind::VarDecl && static_cast<const VarDecl&>(*s).name == var)
        return static_cast<const VarDecl&>(*s).type;
    }
  }
  return nullptr;
}

const FnDecl* NestedFn(const Program& p, const std::string& fname, const std::string& inner) {
  for (const auto& d : p.decls) {
    if (d->kind != DeclKind::Fn || static_cast<const FnDecl&>(*d).name != fname) continue;
    for (const auto& s : static_cast<const FnDecl&>(*d).body) {
      if (s->kind == StmtKind::Fn && static_cast<const FnStmt&>(*s).fn->name == inner)
        return static_cast<const FnStmt&>(*s).fn.get();
    }
  }
  return nullptr;
}

}  // namespace

TEST(SemaOption, SomeInfersOptionType) {
  SrcRun r;
  AnalyzeSrc("a = Some(3)\nb = Some(\"x\")\n", r);
  ASSERT_TRUE(r.ok);
  const auto& sa = static_cast<const Assign&>(*r.prog->stmts[0]);
  EXPECT_EQ(sa.value->type->str(), "Option[int]");
  const auto& sb = static_cast<const Assign&>(*r.prog->stmts[1]);
  EXPECT_EQ(sb.value->type->str(), "Option[str]");
}

TEST(SemaOption, AnnotatedTypeIsOptionOfElem) {
  SrcRun r;
  AnalyzeSrc("fn f() -> int:\n    x: Option[int] = Some(3)\n    return 0\n", r);
  ASSERT_TRUE(r.ok);
  const Type* t = TypeOfLocal(*r.prog, "f", "x");
  ASSERT_NE(t, nullptr);
  EXPECT_EQ(t->kind, TypeKind::Option);
  EXPECT_EQ(t->elem, r.types.intT());
  EXPECT_EQ(t->str(), "Option[int]");
}

TEST(SemaOption, NoneWithoutTypeIsErrorWithHint) {
  SrcRun r;
  AnalyzeSrc("x = None\n", r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("no se puede saber el tipo de None"));
  EXPECT_TRUE(r.hasHint("Option[int]"));
}

TEST(SemaOption, NoneWithAnnotationIsOk) {
  SrcRun r;
  AnalyzeSrc("fn f() -> int:\n    x: Option[int] = None\n    return 0\n", r);
  EXPECT_TRUE(r.ok);
}

TEST(SemaOption, NoneComparisonIsAllowed) {
  SrcRun r;
  AnalyzeSrc("fn f(o: Option[int]) -> bool:\n    return o == None and o != None\n", r);
  EXPECT_TRUE(r.ok);
}

TEST(SemaOption, OptionEqualityWithOptionIsError) {
  SrcRun r;
  AnalyzeSrc("fn f(a: Option[int], b: Option[int]) -> bool:\n    return a == b\n", r);
  EXPECT_FALSE(r.ok);
}

TEST(SemaOption, SomeWithFloatHintRejectsInt) {
  SrcRun r;
  AnalyzeSrc("fn f() -> int:\n    x: Option[float] = Some(1)\n    return 0\n", r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasHint("float(x)"));
}

TEST(SemaOption, DesempacarOTypesAndEsSome) {
  SrcRun r;
  AnalyzeSrc("fn f(o: Option[str]) -> str:\n    if o.es_some():\n        return o.desempacar_o(\"\")\n    return \"no\"\n", r);
  EXPECT_TRUE(r.ok);
}

TEST(SemaOption, DesempacarOWithWrongDefaultIsError) {
  SrcRun r;
  AnalyzeSrc("fn f(o: Option[int]) -> int:\n    return o.desempacar_o(\"s\")\n", r);
  EXPECT_FALSE(r.ok);
}

TEST(SemaOption, UncheckedAccessDoesNotExist) {
  SrcRun r;
  AnalyzeSrc("fn f(o: Option[int]) -> int:\n    return o.valor()\n", r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("no tiene el método 'valor'"));
}

TEST(SemaClosure, ThreeLevelCaptureIsOk) {
  SrcRun r;
  AnalyzeSrc(
      "fn a(x: int) -> int:\n"
      "    fn b() -> int:\n"
      "        fn c() -> int:\n"
      "            return x + 1\n"
      "        return c()\n"
      "    return b()\n",
      r);
  ASSERT_TRUE(r.ok);
  const FnDecl* b = NestedFn(*r.prog, "a", "b");
  ASSERT_NE(b, nullptr);
  // b tiene que capturar x aunque solo la usa su función interna.
  ASSERT_EQ(b->captures.size(), 1u);
  EXPECT_EQ(b->captures[0].name, "x");
  EXPECT_FALSE(b->captures[0].byRef);
}

TEST(SemaClosure, MutCaptureIsByReferenceAndMarksVariable) {
  SrcRun r;
  AnalyzeSrc(
      "fn a() -> int:\n"
      "    mut n = 0\n"
      "    fn inc():\n"
      "        n = n + 1\n"
      "    inc()\n"
      "    return n\n",
      r);
  ASSERT_TRUE(r.ok);
  const FnDecl* inc = NestedFn(*r.prog, "a", "inc");
  ASSERT_NE(inc, nullptr);
  ASSERT_EQ(inc->captures.size(), 1u);
  EXPECT_TRUE(inc->captures[0].byRef);
  const auto& decl = static_cast<const VarDecl&>(*static_cast<const FnDecl&>(*r.prog->decls[0]).body[0]);
  EXPECT_TRUE(decl.captured);
}

TEST(SemaClosure, UseOutsideEndedScopeIsClearError) {
  SrcRun r;
  AnalyzeSrc(
      "fn a() -> int:\n"
      "    if true:\n"
      "        y = 5\n"
      "    fn g() -> int:\n"
      "        return y\n"
      "    return g()\n",
      r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("su ámbito ya terminó"));
}

TEST(SemaClosure, NestedNameNotVisibleOutsideItsBlock) {
  SrcRun r;
  AnalyzeSrc(
      "fn a() -> int:\n"
      "    if true:\n"
      "        fn g() -> int:\n"
      "            return 1\n"
      "    return g()\n",
      r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("función no definida 'g'"));
}

TEST(SemaClosure, AssignToImmutableCaptureIsError) {
  SrcRun r;
  AnalyzeSrc(
      "fn a() -> int:\n"
      "    x = 1\n"
      "    fn g():\n"
      "        x = 2\n"
      "    return x\n",
      r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("variable inmutable 'x'"));
}

TEST(SemaClosure, ReturnedClosureTypeIsFunctionType) {
  SrcRun r;
  AnalyzeSrc(
      "fn mk(k: int) -> fn(int) -> int:\n"
      "    fn f(x: int) -> int:\n"
      "        return x + k\n"
      "    return f\n",
      r);
  ASSERT_TRUE(r.ok);
  const FnDecl* f = NestedFn(*r.prog, "mk", "f");
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->funcType->str(), "fn(int) -> int");
  EXPECT_EQ(r.prog->decls[0]->kind, DeclKind::Fn);
}

TEST(SemaClosure, ClosureCallArityIsChecked) {
  SrcRun r;
  AnalyzeSrc(
      "fn a() -> int:\n"
      "    fn g(x: int) -> int:\n"
      "        return x\n"
      "    return g(1, 2)\n",
      r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("espera 1 argumento"));
}

TEST(SemaClosure, TopLevelFunctionIsNotAValue) {
  SrcRun r;
  AnalyzeSrc("fn sq(a: int) -> int:\n    return a * a\nfn f() -> int:\n    h = sq\n    return 1\n", r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("'sq' no es un valor"));
}

TEST(SemaClosure, GlobalVariableNotVisibleFromNestedFn) {
  SrcRun r;
  AnalyzeSrc("g0 = 1\nfn f() -> int:\n    fn h() -> int:\n        return g0\n    return h()\n", r);
  EXPECT_FALSE(r.ok);
  EXPECT_TRUE(r.hasMsg("no puede usar la variable global 'g0'"));
}

