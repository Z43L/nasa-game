// Pruebas unitarias del lexer y del parser (GTest).
#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <utility>

#include "ast/ast.h"
#include "common/diagnostics.h"
#include "lexer/lexer.h"
#include "parser/parser.h"

using namespace lg;

namespace {

struct Parsed {
  std::unique_ptr<Program> prog;
  Diagnostics diags;
};

Parsed parse(const std::string& src) {
  Parsed r;
  r.prog = parseProgram("t.lg", src, r.diags);
  return r;
}

std::string errors(const Diagnostics& d) {
  std::ostringstream os;
  d.print(os);
  return os.str();
}

// Conversión de nodos del AST (el llamador ya ha comprobado el tipo con `kind`).
template <typename T, typename P>
T* as(const P& p) {
  return static_cast<T*>(p.get());
}

bool hasMessage(const Diagnostics& d, const std::string& fragment) {
  for (const Diagnostic& item : d.all()) {
    if (item.message.find(fragment) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

// ============================================================== Lexer

TEST(Lexer, PositionsAreOneBasedAndIndentTokensEmitted) {
  Diagnostics d;
  auto toks = tokenize("t.lg", "x = 10\n  y\n", d);
  ASSERT_FALSE(d.hasErrors()) << errors(d);
  ASSERT_EQ(toks.size(), 9u);
  EXPECT_EQ(toks[0].kind, TokenKind::Ident);
  EXPECT_EQ(toks[0].line, 1);
  EXPECT_EQ(toks[0].col, 1);
  EXPECT_EQ(toks[1].kind, TokenKind::Assign);
  EXPECT_EQ(toks[1].col, 3);
  EXPECT_EQ(toks[2].kind, TokenKind::Int);
  EXPECT_EQ(toks[2].intValue, 10);
  EXPECT_EQ(toks[2].col, 5);
  EXPECT_EQ(toks[3].kind, TokenKind::Newline);
  EXPECT_EQ(toks[4].kind, TokenKind::Indent);
  EXPECT_EQ(toks[5].kind, TokenKind::Ident);
  EXPECT_EQ(toks[5].line, 2);
  EXPECT_EQ(toks[5].col, 3);
  EXPECT_EQ(toks[6].kind, TokenKind::Newline);
  EXPECT_EQ(toks[7].kind, TokenKind::Dedent);
  EXPECT_EQ(toks[8].kind, TokenKind::End);
}

TEST(Lexer, RangeIsNotAFloat) {
  Diagnostics d;
  auto toks = tokenize("t.lg", "1..10\n", d);
  ASSERT_FALSE(d.hasErrors());
  EXPECT_EQ(toks[0].kind, TokenKind::Int);
  EXPECT_EQ(toks[1].kind, TokenKind::DotDot);
  EXPECT_EQ(toks[2].kind, TokenKind::Int);
  EXPECT_EQ(toks[2].intValue, 10);
}

TEST(Lexer, FloatLiterals) {
  Diagnostics d;
  auto toks = tokenize("t.lg", "1.0 1e3 2.5E-2\n", d);
  ASSERT_FALSE(d.hasErrors());
  EXPECT_EQ(toks[0].kind, TokenKind::Float);
  EXPECT_DOUBLE_EQ(toks[0].floatValue, 1.0);
  EXPECT_EQ(toks[1].kind, TokenKind::Float);
  EXPECT_DOUBLE_EQ(toks[1].floatValue, 1000.0);
  EXPECT_DOUBLE_EQ(toks[2].floatValue, 0.025);
}

TEST(Lexer, StringEscapes) {
  Diagnostics d;
  auto toks = tokenize("t.lg", R"(s = "a\tb\\c\"d\n")"
                                "\n",
                       d);
  ASSERT_FALSE(d.hasErrors()) << errors(d);
  EXPECT_EQ(toks[2].kind, TokenKind::String);
  EXPECT_EQ(toks[2].text, "a\tb\\c\"d\n");
}

TEST(Lexer, CommentsAndBlankLinesDoNotChangeIndentation) {
  Diagnostics d;
  auto toks = tokenize("t.lg", "if x:\n\n    # comentario\n    y = 1\n", d);
  ASSERT_FALSE(d.hasErrors()) << errors(d);
  int indents = 0;
  for (const Token& t : toks) indents += t.kind == TokenKind::Indent;
  EXPECT_EQ(indents, 1);
}

TEST(Lexer, TabIsAnError) {
  Diagnostics d;
  tokenize("t.lg", "x = 1\n\ty = 2\n", d);
  ASSERT_TRUE(d.hasErrors());
  EXPECT_TRUE(hasMessage(d, "tabulación"));
  EXPECT_EQ(d.all()[0].loc.line, 2);
  EXPECT_EQ(d.all()[0].loc.col, 1);
}

TEST(Lexer, InconsistentDedentIsAnError) {
  Diagnostics d;
  tokenize("t.lg", "if a:\n        b = 1\n    c = 2\n", d);
  ASSERT_TRUE(d.hasErrors());
  EXPECT_TRUE(hasMessage(d, "indentación inconsistente"));
  EXPECT_EQ(d.all()[0].loc.line, 3);
}

TEST(Lexer, UnterminatedStringIsAnError) {
  Diagnostics d;
  tokenize("t.lg", "s = \"hola\n", d);
  ASSERT_TRUE(d.hasErrors());
  EXPECT_TRUE(hasMessage(d, "cadena sin cerrar"));
  EXPECT_EQ(d.all()[0].loc.line, 1);
  EXPECT_EQ(d.all()[0].loc.col, 5);
}

TEST(Lexer, SemicolonIsAnErrorWithHint) {
  Diagnostics d;
  tokenize("t.lg", "x = 1;\n", d);
  ASSERT_TRUE(d.hasErrors());
  EXPECT_EQ(d.all()[0].loc.col, 6);
  EXPECT_NE(d.all()[0].hint.find("punto y coma"), std::string::npos);
}

// ============================================================== Parser: bloques e indentación

TEST(ParserIndent, BlockEndsWithDedent) {
  auto r = parse("if x:\n    y = 1\n    z = 2\nw = 3\n");
  ASSERT_FALSE(r.diags.hasErrors()) << errors(r.diags);
  ASSERT_EQ(r.prog->stmts.size(), 2u);
  auto* ifs = as<IfStmt>(r.prog->stmts[0]);
  EXPECT_EQ(ifs->thenBody.size(), 2u);
  EXPECT_EQ(r.prog->stmts[1]->kind, StmtKind::Assign);
}

TEST(ParserIndent, UnexpectedIndentIsReportedAndRecovered) {
  auto r = parse("x = 1\n    y = 2\nz = 3\n");
  EXPECT_TRUE(r.diags.hasErrors());
  EXPECT_TRUE(hasMessage(r.diags, "indentación inesperada"));
  ASSERT_EQ(r.prog->stmts.size(), 2u);  // x y z: solo se pierde y
}

TEST(ParserIndent, MissingBlockIsAnError) {
  auto r = parse("if x:\nfoo = 1\n");
  EXPECT_TRUE(hasMessage(r.diags, "se esperaba un bloque indentado"));
}

// ============================================================== Parser: control de flujo

TEST(ParserControl, IfElifElseDesugarsElifIntoElse) {
  auto r = parse("if a:\n    b = 1\nelif c:\n    b = 2\nelse:\n    b = 3\n");
  ASSERT_FALSE(r.diags.hasErrors()) << errors(r.diags);
  auto* top = as<IfStmt>(r.prog->stmts[0]);
  ASSERT_EQ(top->elseBody.size(), 1u);
  ASSERT_EQ(top->elseBody[0]->kind, StmtKind::If);
  auto* elif = as<IfStmt>(top->elseBody[0]);
  EXPECT_EQ(elif->thenBody.size(), 1u);
  EXPECT_EQ(elif->elseBody.size(), 1u);
  EXPECT_NE(elif->elseBody[0]->kind, StmtKind::If);
}

TEST(ParserControl, ForWithRange) {
  auto r = parse("for i in 0..10:\n    print(i)\n");
  ASSERT_FALSE(r.diags.hasErrors()) << errors(r.diags);
  auto* f = as<ForStmt>(r.prog->stmts[0]);
  EXPECT_EQ(f->var, "i");
  ASSERT_EQ(f->iter->kind, ExprKind::Range);
  auto* range = as<RangeExpr>(f->iter);
  EXPECT_EQ(as<IntLit>(range->start)->value, 0);
  EXPECT_EQ(as<IntLit>(range->end)->value, 10);
  EXPECT_EQ(f->body.size(), 1u);
}

TEST(ParserControl, ForOverList) {
  auto r = parse("for x in lista:\n    print(x)\n");
  ASSERT_FALSE(r.diags.hasErrors()) << errors(r.diags);
  auto* f = as<ForStmt>(r.prog->stmts[0]);
  EXPECT_EQ(f->iter->kind, ExprKind::Ident);
}

TEST(ParserControl, WhileBreakContinue) {
  auto r = parse("while x < 3:\n    x = x + 1\n    if x == 2:\n        break\n    continue\n");
  ASSERT_FALSE(r.diags.hasErrors()) << errors(r.diags);
  auto* w = as<WhileStmt>(r.prog->stmts[0]);
  ASSERT_EQ(w->body.size(), 3u);
  EXPECT_EQ(w->body[2]->kind, StmtKind::Continue);
}

TEST(ParserControl, RangeOutsideForIsAnError) {
  auto r = parse("x = 0..10\n");
  ASSERT_TRUE(r.diags.hasErrors());
  EXPECT_TRUE(hasMessage(r.diags, "'..' solo puede usarse en la cabecera de un for"));
  EXPECT_EQ(r.diags.all()[0].loc.line, 1);
  EXPECT_EQ(r.diags.all()[0].loc.col, 6);
}

// ============================================================== Parser: declaraciones

TEST(ParserDecl, VarDeclMutAndTyped) {
  auto r = parse("mut y = 20\nx: int = 3\nx = 10\n");
  ASSERT_FALSE(r.diags.hasErrors()) << errors(r.diags);
  auto* y = as<VarDecl>(r.prog->stmts[0]);
  EXPECT_TRUE(y->isMut);
  EXPECT_EQ(y->annot, nullptr);
  auto* x = as<VarDecl>(r.prog->stmts[1]);
  EXPECT_FALSE(x->isMut);
  ASSERT_NE(x->annot, nullptr);
  EXPECT_EQ(x->annot->name, "int");
  auto* a = as<Assign>(r.prog->stmts[2]);
  EXPECT_EQ(a->target->kind, ExprKind::Ident);
}

TEST(ParserDecl, AssignToIndexAndMember) {
  auto r = parse("lista[0] = 5\np.x = 1.0\n");
  ASSERT_FALSE(r.diags.hasErrors()) << errors(r.diags);
  EXPECT_EQ(as<Assign>(r.prog->stmts[0])->target->kind, ExprKind::Index);
  EXPECT_EQ(as<Assign>(r.prog->stmts[1])->target->kind, ExprKind::Member);
}

TEST(ParserDecl, FnWithReturn) {
  auto r = parse("fn suma(a: int, b: int) -> int:\n    return a + b\n");
  ASSERT_FALSE(r.diags.hasErrors()) << errors(r.diags);
  ASSERT_EQ(r.prog->decls.size(), 1u);
  ASSERT_EQ(r.prog->decls[0]->kind, DeclKind::Fn);
  auto* fn = as<FnDecl>(r.prog->decls[0]);
  EXPECT_EQ(fn->name, "suma");
  ASSERT_EQ(fn->params.size(), 2u);
  EXPECT_EQ(fn->params[1].name, "b");
  EXPECT_FALSE(fn->params[1].isSelf);
  ASSERT_NE(fn->retAnnot, nullptr);
  EXPECT_EQ(fn->retAnnot->name, "int");
  EXPECT_TRUE(fn->hasBody);
  EXPECT_FALSE(fn->isExtern);
  ASSERT_EQ(fn->body.size(), 1u);
  ASSERT_EQ(fn->body[0]->kind, StmtKind::Return);
  auto* ret = as<ReturnStmt>(fn->body[0]);
  EXPECT_EQ(as<Binary>(ret->value)->op, BinaryOp::Add);
}

TEST(ParserDecl, FnWithoutReturnType) {
  auto r = parse("fn saluda(nombre: str):\n    print(nombre)\n");
  ASSERT_FALSE(r.diags.hasErrors()) << errors(r.diags);
  auto* fn = as<FnDecl>(r.prog->decls[0]);
  EXPECT_EQ(fn->retAnnot, nullptr);
}

TEST(ParserDecl, ImplWithSelf) {
  auto r = parse(
      "impl Punto:\n"
      "    fn norma(self) -> float:\n"
      "        return 0.0\n"
      "    fn mover(mut self, dx: float):\n"
      "        self.x = dx\n");
  ASSERT_FALSE(r.diags.hasErrors()) << errors(r.diags);
  auto* impl = as<ImplDecl>(r.prog->decls[0]);
  EXPECT_EQ(impl->typeName, "Punto");
  ASSERT_EQ(impl->methods.size(), 2u);
  const FnDecl& norma = *impl->methods[0];
  ASSERT_EQ(norma.params.size(), 1u);
  EXPECT_TRUE(norma.params[0].isSelf);
  EXPECT_FALSE(norma.params[0].selfMut);
  EXPECT_EQ(norma.params[0].name, "self");
  const FnDecl& mover = *impl->methods[1];
  EXPECT_TRUE(mover.params[0].isSelf);
  EXPECT_TRUE(mover.params[0].selfMut);
  EXPECT_EQ(mover.params[1].annot->name, "float");
  ASSERT_EQ(mover.body.size(), 1u);
  EXPECT_EQ(as<Assign>(mover.body[0])->target->kind, ExprKind::Member);
}

TEST(ParserDecl, SelfOutsideImplIsAnError) {
  auto r = parse("fn f(self):\n    return\n");
  EXPECT_TRUE(hasMessage(r.diags, "'self' solo es válido en métodos"));
}

TEST(ParserDecl, ExternHasNoBody) {
  auto r = parse("extern \"C\" fn sqrt(x: float) -> float\n");
  ASSERT_FALSE(r.diags.hasErrors()) << errors(r.diags);
  auto* fn = as<FnDecl>(r.prog->decls[0]);
  EXPECT_TRUE(fn->isExtern);
  EXPECT_FALSE(fn->hasBody);
  EXPECT_EQ(fn->abi, "C");
  EXPECT_EQ(fn->name, "sqrt");
  EXPECT_TRUE(fn->body.empty());
  EXPECT_EQ(fn->retAnnot->name, "float");
}

TEST(ParserDecl, ExternWithColonIsAnError) {
  auto r = parse("extern \"C\" fn sqrt(x: float) -> float:\n    return x\n");
  EXPECT_TRUE(hasMessage(r.diags, "las funciones extern no tienen cuerpo"));
}

TEST(ParserDecl, StructFields) {
  auto r = parse("struct Punto:\n    x: float\n    y: float\n");
  ASSERT_FALSE(r.diags.hasErrors()) << errors(r.diags);
  auto* st = as<StructDecl>(r.prog->decls[0]);
  EXPECT_EQ(st->name, "Punto");
  ASSERT_EQ(st->fields.size(), 2u);
  EXPECT_EQ(st->fields[0].name, "x");
  EXPECT_EQ(st->fields[1].name, "y");
  EXPECT_EQ(st->fields[1].annot->name, "float");
}

TEST(ParserDecl, ImportsIncludingSubpath) {
  auto r = parse("import util\nimport carpeta/util\n");
  ASSERT_FALSE(r.diags.hasErrors()) << errors(r.diags);
  ASSERT_EQ(r.prog->decls.size(), 2u);
  EXPECT_EQ(as<ImportDecl>(r.prog->decls[0])->path, "util");
  EXPECT_EQ(as<ImportDecl>(r.prog->decls[1])->path, "carpeta/util");
}

TEST(ParserDecl, ImportInsideBlockIsAnError) {
  auto r = parse("fn f():\n    import util\n");
  EXPECT_TRUE(hasMessage(r.diags, "solo puede declararse a nivel superior"));
}

TEST(ParserDecl, TopLevelMixesDeclsAndStatements) {
  auto r = parse("x = 1\nfn f():\n    return\ny = 2\n");
  ASSERT_FALSE(r.diags.hasErrors()) << errors(r.diags);
  EXPECT_EQ(r.prog->decls.size(), 1u);
  EXPECT_EQ(r.prog->stmts.size(), 2u);
  EXPECT_EQ(as<ReturnStmt>(as<FnDecl>(r.prog->decls[0])->body[0])->value, nullptr);
}

// ============================================================== Tipos

TEST(ParserTypes, TensorDimsAndListDict) {
  auto r = parse("t: Tensor[float, (2, 3)] = zeros(2, 3)\nv: Tensor[float, (128)] = zeros(128)\n"
                 "l: List[Dict[str, int]] = []\n");
  ASSERT_FALSE(r.diags.hasErrors()) << errors(r.diags);
  auto* t = as<VarDecl>(r.prog->stmts[0]);
  EXPECT_EQ(t->annot->kind, TypeExpr::Kind::Tensor);
  EXPECT_EQ(t->annot->dims, (std::vector<int64_t>{2, 3}));
  EXPECT_EQ(t->annot->args[0]->name, "float");
  auto* v = as<VarDecl>(r.prog->stmts[1]);
  EXPECT_EQ(v->annot->dims, (std::vector<int64_t>{128}));
  auto* l = as<VarDecl>(r.prog->stmts[2]);
  EXPECT_EQ(l->annot->kind, TypeExpr::Kind::List);
  EXPECT_EQ(l->annot->args[0]->kind, TypeExpr::Kind::Dict);
  EXPECT_EQ(l->annot->args[0]->args[1]->name, "int");
}

TEST(ParserTypes, TensorOfStrIsAnError) {
  auto r = parse("t: Tensor[str, (2)] = zeros(2)\n");
  EXPECT_TRUE(hasMessage(r.diags, "Tensor solo admite elementos int o float"));
}

TEST(ParserTypes, TensorZeroDimIsAnError) {
  auto r = parse("t: Tensor[float, (0)] = zeros(0)\n");
  EXPECT_TRUE(hasMessage(r.diags, "la dimensión debe ser mayor que 0"));
}

// ============================================================== Literales y colecciones

TEST(ParserLiterals, ListAndDict) {
  auto r = parse("l = [1, 2]\nd = {\"ana\": 30}\n");
  ASSERT_FALSE(r.diags.hasErrors()) << errors(r.diags);
  auto* la = as<Assign>(r.prog->stmts[0]);
  ASSERT_EQ(la->value->kind, ExprKind::ListLit);
  EXPECT_EQ(as<ListLit>(la->value)->elems.size(), 2u);
  auto* da = as<Assign>(r.prog->stmts[1]);
  ASSERT_EQ(da->value->kind, ExprKind::DictLit);
  auto* dict = as<DictLit>(da->value);
  ASSERT_EQ(dict->entries.size(), 1u);
  EXPECT_EQ(as<StrLit>(dict->entries[0].first)->value, "ana");
}

TEST(ParserLiterals, CallWithNamedArgs) {
  auto r = parse("p = Punto(x: 1.0, y: 2.0)\n");
  ASSERT_FALSE(r.diags.hasErrors()) << errors(r.diags);
  auto* call = as<Call>(as<Assign>(r.prog->stmts[0])->value);
  EXPECT_EQ(call->callee->kind, ExprKind::Ident);
  EXPECT_EQ(call->argNames, (std::vector<std::string>{"x", "y"}));
  EXPECT_EQ(call->args.size(), 2u);
}

TEST(ParserLiterals, PositionalAfterNamedIsAnError) {
  auto r = parse("p = Punto(x: 1.0, 2.0)\n");
  EXPECT_TRUE(hasMessage(r.diags, "argumento posicional después de un argumento con nombre"));
}

TEST(ParserLiterals, StringEscapesReachTheAst) {
  auto r = parse(R"(s = "a\nb")"
                 "\n");
  ASSERT_FALSE(r.diags.hasErrors()) << errors(r.diags);
  EXPECT_EQ(as<StrLit>(as<Assign>(r.prog->stmts[0])->value)->value, "a\nb");
}

TEST(ParserLiterals, MethodCallAndIndexChain) {
  auto r = parse("lista.append(a[i + 1])\n");
  ASSERT_FALSE(r.diags.hasErrors()) << errors(r.diags);
  auto* call = as<Call>(as<ExprStmt>(r.prog->stmts[0])->expr);
  EXPECT_EQ(call->callee->kind, ExprKind::Member);
  ASSERT_EQ(call->args.size(), 1u);
  EXPECT_EQ(call->args[0]->kind, ExprKind::Index);
}

// ============================================================== Precedencia

TEST(ParserPrecedence, MulBindsTighterThanAdd) {
  auto r = parse("a + b * c\n");
  auto* add = as<Binary>(as<ExprStmt>(r.prog->stmts[0])->expr);
  EXPECT_EQ(add->op, BinaryOp::Add);
  EXPECT_EQ(as<Binary>(add->rhs)->op, BinaryOp::Mul);
}

TEST(ParserPrecedence, OrIsLowerThanAnd) {
  auto r = parse("a or b and c\n");
  auto* orE = as<Binary>(as<ExprStmt>(r.prog->stmts[0])->expr);
  EXPECT_EQ(orE->op, BinaryOp::Or);
  EXPECT_EQ(as<Binary>(orE->rhs)->op, BinaryOp::And);
}

TEST(ParserPrecedence, NotAppliesToComparison) {
  auto r = parse("not a == b\n");
  auto* n = as<Unary>(as<ExprStmt>(r.prog->stmts[0])->expr);
  EXPECT_EQ(n->op, UnaryOp::Not);
  EXPECT_EQ(as<Binary>(n->operand)->op, BinaryOp::Eq);
}

TEST(ParserPrecedence, ComparisonBindsTighterThanAnd) {
  auto r = parse("a < b and c in d\n");
  auto* andE = as<Binary>(as<ExprStmt>(r.prog->stmts[0])->expr);
  EXPECT_EQ(andE->op, BinaryOp::And);
  EXPECT_EQ(as<Binary>(andE->lhs)->op, BinaryOp::Lt);
  EXPECT_EQ(as<Binary>(andE->rhs)->op, BinaryOp::In);
}

TEST(ParserPrecedence, UnaryMinusBindsTighterThanMul) {
  auto r = parse("-a * b\n");
  auto* mul = as<Binary>(as<ExprStmt>(r.prog->stmts[0])->expr);
  EXPECT_EQ(mul->op, BinaryOp::Mul);
  EXPECT_EQ(as<Unary>(mul->lhs)->op, UnaryOp::Neg);
}

TEST(ParserPrecedence, MatMulSameLevelAsMul) {
  auto r = parse("a @ b * c\n");
  auto* mul = as<Binary>(as<ExprStmt>(r.prog->stmts[0])->expr);
  EXPECT_EQ(mul->op, BinaryOp::Mul);
  EXPECT_EQ(as<Binary>(mul->lhs)->op, BinaryOp::MatMul);
}

TEST(ParserPrecedence, PostfixBindsTighterThanUnary) {
  auto r = parse("-a.b\n");
  auto* neg = as<Unary>(as<ExprStmt>(r.prog->stmts[0])->expr);
  EXPECT_EQ(neg->operand->kind, ExprKind::Member);
}

// ============================================================== Errores: posición y recuperación

TEST(ParserErrors, MissingColonAfterIfReportsLineAndColumn) {
  auto r = parse("if x\n    y = 1\n");
  ASSERT_TRUE(r.diags.hasErrors());
  const Diagnostic& e = r.diags.all()[0];
  EXPECT_EQ(e.message, "se esperaba ':' después de la condición del if");
  EXPECT_EQ(e.hint, "añade ':' al final de la línea");
  EXPECT_EQ(e.loc.line, 1);
  EXPECT_EQ(e.loc.col, 5);
  EXPECT_EQ(e.loc.file, "t.lg");
  // El bloque se sigue parseando: no hay errores en cascada.
  EXPECT_EQ(r.diags.errorCount(), 1u);
}

TEST(ParserErrors, MissingColonAfterWhileAndFn) {
  EXPECT_TRUE(hasMessage(parse("while x\n    y = 1\n").diags, "después de la condición del while"));
  EXPECT_TRUE(hasMessage(parse("fn f()\n    return\n").diags, "después de la firma de la función"));
}

TEST(ParserErrors, ElseWithoutIf) {
  auto r = parse("x = 1\nelse:\n    y = 2\n");
  EXPECT_TRUE(hasMessage(r.diags, "'else' sin 'if' correspondiente"));
}

TEST(ParserErrors, MultipleErrorsAreRecovered) {
  auto r = parse("a = = 1\nb = 2\nc = 3 4\nd = 5\n");
  EXPECT_EQ(r.diags.errorCount(), 2u);
  ASSERT_GE(r.diags.all().size(), 2u);
  EXPECT_EQ(r.diags.all()[0].loc.line, 1);
  EXPECT_EQ(r.diags.all()[1].loc.line, 3);
  EXPECT_EQ(r.diags.all()[1].loc.col, 7);
  // Las sentencias correctas siguen en el AST.
  EXPECT_EQ(r.prog->stmts.size(), 2u);
}

TEST(ParserErrors, ErrorInsideBlockDoesNotLoseNextBlock) {
  auto r = parse("fn f():\n    x = = 1\n    y = 2\nz = 3\n");
  EXPECT_EQ(r.diags.errorCount(), 1u);
  ASSERT_EQ(r.prog->decls.size(), 1u);
  EXPECT_EQ(as<FnDecl>(r.prog->decls[0])->body.size(), 1u);  // solo y = 2
  EXPECT_EQ(r.prog->stmts.size(), 1u);                        // z = 3
}

TEST(ParserErrors, MissingStatementEndHasHint) {
  auto r = parse("x = 1 2\n");
  ASSERT_TRUE(r.diags.hasErrors());
  EXPECT_TRUE(hasMessage(r.diags, "se esperaba fin de línea"));
  EXPECT_EQ(r.diags.all()[0].loc.col, 7);
}

TEST(ParserErrors, TabInsideProgramStillProducesTree) {
  auto r = parse("x = 1\n\ty = 2\nz = 3\n");
  EXPECT_TRUE(r.diags.hasErrors());
  ASSERT_NE(r.prog, nullptr);
}

TEST(ParserErrors, UnclosedParenIsReported) {
  auto r = parse("p = f(1, 2\n");
  EXPECT_TRUE(r.diags.hasErrors());
  EXPECT_NE(r.prog, nullptr);
}

TEST(ParserErrors, EmptySourceIsValid) {
  auto r = parse("");
  EXPECT_FALSE(r.diags.hasErrors());
  EXPECT_TRUE(r.prog->decls.empty());
  EXPECT_TRUE(r.prog->stmts.empty());
}

// ============================================================== Límites de anidación

TEST(ParserLimits, DeepParenthesesDoNotCrashAndRecoverOnce) {
  const std::string src = "x = " + std::string(5000, '(') + "1" + std::string(5000, ')') + "\ny = 2\n";
  auto r = parse(src);
  EXPECT_EQ(r.diags.errorCount(), 1u) << errors(r.diags);
  EXPECT_TRUE(hasMessage(r.diags, "expresión demasiado anidada (límite 1000 niveles)"));
  ASSERT_FALSE(r.diags.all().empty());
  EXPECT_EQ(r.diags.all()[0].loc.line, 1);
  EXPECT_EQ(r.diags.all()[0].hint, "divide la expresión en variables intermedias");
  // La sentencia siguiente se sigue parseando.
  EXPECT_EQ(r.prog->stmts.size(), 1u);
}

TEST(ParserLimits, DeepListsAndUnaryDoNotCrash) {
  auto lists = parse("x = " + std::string(20000, '[') + "1" + std::string(20000, ']') + "\n");
  EXPECT_EQ(lists.diags.errorCount(), 1u);
  auto unary = parse("x = " + std::string(20000, '-') + " 1\n");
  EXPECT_EQ(unary.diags.errorCount(), 1u);
}

TEST(ParserLimits, OneThousandNestedListsAreAccepted) {
  auto r = parse("x = " + std::string(1000, '[') + "1" + std::string(1000, ']') + "\n");
  EXPECT_FALSE(r.diags.hasErrors()) << errors(r.diags);
}

TEST(ParserLimits, DeepBlocksDoNotCrashAndReportOnce) {
  std::string src;
  const int levels = 1005;
  for (int i = 0; i < levels; ++i) src += std::string(i, ' ') + "if true:\n";
  src += std::string(levels, ' ') + "x = 1\n";
  src += "y = 2\n";
  auto r = parse(src);
  EXPECT_EQ(r.diags.errorCount(), 1u) << errors(r.diags);
  EXPECT_TRUE(hasMessage(r.diags, "bloque demasiado anidado (límite 1000 niveles)"));
  EXPECT_EQ(r.prog->stmts.size(), 2u);  // el if raíz y y = 2
}

TEST(ParserLimits, LongElifChainDoesNotCrash) {
  std::string src = "if true:\n    x = 1\n";
  for (int i = 0; i < 3000; ++i) src += "elif true:\n    x = 1\n";
  auto r = parse(src);
  EXPECT_EQ(r.diags.errorCount(), 1u) << errors(r.diags);
}

// ============================================================== BOM

TEST(LexerBom, Utf8ByteOrderMarkIsIgnored) {
  Diagnostics d;
  auto toks = tokenize("t.lg", std::string("\xEF\xBB\xBF") + "x = 1\n", d);
  EXPECT_FALSE(d.hasErrors()) << errors(d);
  ASSERT_FALSE(toks.empty());
  EXPECT_EQ(toks[0].kind, TokenKind::Ident);
  EXPECT_EQ(toks[0].line, 1);
  EXPECT_EQ(toks[0].col, 1);
}

// ============================================================== Literales numéricos

TEST(LexerNumbers, WellFormedLiteralsAreAccepted) {
  Diagnostics d;
  auto toks = tokenize("t.lg", "a = 1\nb = 1.5\nc = 1e3\nd = 2.5e-2\nr = 1..10\n", d);
  EXPECT_FALSE(d.hasErrors()) << errors(d);
  int dots = 0;
  for (const Token& t : toks) dots += t.kind == TokenKind::DotDot;
  EXPECT_EQ(dots, 1);  // 1..10 sigue siendo un rango
  bool sawFloat = false;
  for (const Token& t : toks) {
    if (t.kind == TokenKind::Float && t.text == "1.5") {
      EXPECT_DOUBLE_EQ(t.floatValue, 1.5);
      sawFloat = true;
    }
  }
  EXPECT_TRUE(sawFloat);
}

TEST(LexerNumbers, MalformedLiteralsReadWholeTokenWithPosition) {
  const std::pair<std::string, std::string> cases[] = {
      {"0x10", "0x10"}, {"1e", "1e"},       {"1e+", "1e+"}, {"1_000", "1_000"},
      {"1.2.3", "1.2.3"}, {"3abc", "3abc"}, {"1.", "1."},
  };
  for (const auto& c : cases) {
    Diagnostics d;
    tokenize("t.lg", "x = " + c.first + "\n", d);
    ASSERT_EQ(d.errorCount(), 1u) << c.first << errors(d);
    const Diagnostic& e = d.all()[0];
    EXPECT_EQ(e.message, "literal numérico no válido '" + c.second + "'") << c.first;
    EXPECT_EQ(e.loc.line, 1) << c.first;
    EXPECT_EQ(e.loc.col, 5) << c.first;
    EXPECT_FALSE(e.hint.empty()) << c.first;
  }
}

TEST(LexerNumbers, MalformedLiteralHintsSuggestTheFix) {
  Diagnostics hex;
  tokenize("t.lg", "x = 0x10\n", hex);
  ASSERT_EQ(hex.errorCount(), 1u);
  EXPECT_EQ(hex.all()[0].hint, "los enteros no admiten prefijo 0x; escribe 16 en decimal");

  Diagnostics sep;
  tokenize("t.lg", "x = 1_000\n", sep);
  ASSERT_EQ(sep.errorCount(), 1u);
  EXPECT_EQ(sep.all()[0].hint, "los separadores de miles no existen; escribe 1000");
}

TEST(LexerNumbers, MalformedLiteralDoesNotCascadeIntoParserErrors) {
  auto r = parse("x = 0x10\ny = 2\n");
  EXPECT_EQ(r.diags.errorCount(), 1u) << errors(r.diags);
  EXPECT_EQ(r.prog->stmts.size(), 2u);
}

// ============================================================== Argumentos con nombre

TEST(ParserNamedArgs, EqualsInArgumentSuggestsColon) {
  auto r = parse("r = f(a=1)\n");
  EXPECT_EQ(r.diags.errorCount(), 1u) << errors(r.diags);
  ASSERT_FALSE(r.diags.all().empty());
  EXPECT_EQ(r.diags.all()[0].message, "para argumentos con nombre usa ':' (nombre: valor), no '='");
  EXPECT_NE(r.diags.all()[0].hint.find("a: 1"), std::string::npos);
  EXPECT_EQ(r.diags.all()[0].loc.col, 8);  // posición del '='
}

TEST(ParserNamedArgs, ColonSyntaxStillParses) {
  auto r = parse("r = f(b: 2)\n");
  EXPECT_FALSE(r.diags.hasErrors()) << errors(r.diags);
}

// ============================================================== Límite de errores impresos

TEST(DiagnosticsPrint, AtMostTwentyErrorsArePrinted) {
  Diagnostics d;
  for (int i = 0; i < 50000; ++i) d.error(Loc{"t.lg", 1, 1}, "error repetido");
  EXPECT_EQ(d.errorCount(), 50000u);
  EXPECT_TRUE(d.hasErrors());
  std::ostringstream os;
  d.print(os);
  const std::string out = os.str();
  size_t printed = 0;
  for (size_t p = out.find(": error: "); p != std::string::npos; p = out.find(": error: ", p + 1)) ++printed;
  EXPECT_EQ(printed, 20u);
  EXPECT_NE(out.find("demasiados errores, se omiten 49980 más"), std::string::npos);
}

TEST(DiagnosticsPrint, FewErrorsPrintWithoutOmissionLine) {
  Diagnostics d;
  d.error(Loc{"t.lg", 1, 1}, "uno");
  std::ostringstream os;
  d.print(os);
  EXPECT_EQ(os.str().find("demasiados errores"), std::string::npos);
}
