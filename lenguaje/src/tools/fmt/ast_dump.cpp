// Volcado canónico del AST para lgfmt: sin posiciones ni campos de sema, con valores exactos.
// Dos programas con el mismo volcado tienen el mismo árbol. Se usa para verificar que el formato
// no cambia el código (ver formatSource).
//
// DÓNDE AÑADIR CASOS NUEVOS: igual que en printer.cpp, añade el caso de cada ExprKind, StmtKind,
// DeclKind o TypeExpr::Kind nuevo en su switch (dumpType, dumpExpr, dumpStmt, dumpDecl).
#include <cstdio>
#include <string>
#include <vector>

#include "format.h"

namespace lgfmt {
namespace {

using namespace lg;

std::string hexFloat(double v) {
  char buf[64];
  std::snprintf(buf, sizeof buf, "%a", v);
  return buf;
}

const char* binName(BinaryOp op) {
  switch (op) {
    case BinaryOp::Add: return "add";
    case BinaryOp::Sub: return "sub";
    case BinaryOp::Mul: return "mul";
    case BinaryOp::Div: return "div";
    case BinaryOp::Mod: return "mod";
    case BinaryOp::MatMul: return "matmul";
    case BinaryOp::Eq: return "eq";
    case BinaryOp::Ne: return "ne";
    case BinaryOp::Lt: return "lt";
    case BinaryOp::Gt: return "gt";
    case BinaryOp::Le: return "le";
    case BinaryOp::Ge: return "ge";
    case BinaryOp::And: return "and";
    case BinaryOp::Or: return "or";
    case BinaryOp::In: return "in";
  }
  return "?";
}

std::string dumpType(const TypeExpr* t) {
  if (!t) return "_";
  std::string s = "(type ";
  switch (t->kind) {
    case TypeExpr::Kind::Name: s += "name " + t->name; break;
    case TypeExpr::Kind::List: s += "list"; break;
    case TypeExpr::Kind::Dict: s += "dict"; break;
    case TypeExpr::Kind::Tensor: s += "tensor"; break;
    case TypeExpr::Kind::Option: s += "option"; break;
    case TypeExpr::Kind::Func: s += "func"; break;
    case TypeExpr::Kind::Hilo: s += "hilo"; break;
    case TypeExpr::Kind::Canal: s += "canal"; break;
    case TypeExpr::Kind::Debil: s += "debil"; break;
  }
  for (const auto& a : t->args) s += " " + dumpType(a.get());
  if (!t->dims.empty()) {
    s += " dims";
    for (int64_t d : t->dims) s += " " + std::to_string(d);
  }
  if (t->ret) s += " ret " + dumpType(t->ret.get());
  return s + ")";
}

std::string dumpExpr(const Expr* e);
std::string dumpStmt(const Stmt* s);
std::string dumpDecl(const Decl* d);

std::string dumpBlock(const std::vector<StmtPtr>& body) {
  std::string s = "(block";
  for (const auto& st : body) s += " " + dumpStmt(st.get());
  return s + ")";
}

// Añadir aquí los casos de nuevas ExprKind.
std::string dumpExpr(const Expr* e) {
  if (!e) return "_";
  switch (e->kind) {
    case ExprKind::IntLit: return "(int " + std::to_string(static_cast<const IntLit*>(e)->value) + ")";
    case ExprKind::FloatLit: return "(float " + hexFloat(static_cast<const FloatLit*>(e)->value) + ")";
    case ExprKind::BoolLit: return std::string("(bool ") + (static_cast<const BoolLit*>(e)->value ? "1" : "0") + ")";
    case ExprKind::StrLit: {
      const std::string& v = static_cast<const StrLit*>(e)->value;
      return "(str " + std::to_string(v.size()) + ":" + v + ")";
    }
    case ExprKind::NoneLit: return "(none)";
    case ExprKind::Await: return "(await " + dumpExpr(static_cast<const AwaitExpr*>(e)->operand.get()) + ")";
    case ExprKind::Ident: return "(id " + static_cast<const Ident*>(e)->name + ")";
    case ExprKind::Unary: {
      const auto* u = static_cast<const Unary*>(e);
      return std::string(u->op == UnaryOp::Neg ? "(neg " : "(not ") + dumpExpr(u->operand.get()) + ")";
    }
    case ExprKind::Binary: {
      const auto* b = static_cast<const Binary*>(e);
      return std::string("(bin ") + binName(b->op) + " " + dumpExpr(b->lhs.get()) + " " +
             dumpExpr(b->rhs.get()) + ")";
    }
    case ExprKind::Call: {
      const auto* c = static_cast<const Call*>(e);
      std::string s = "(call " + dumpExpr(c->callee.get()) + " [";
      for (size_t i = 0; i < c->args.size(); ++i) {
        const std::string name = i < c->argNames.size() ? c->argNames[i] : "";
        s += " (" + name + " " + dumpExpr(c->args[i].get()) + ")";
      }
      return s + " ])";
    }
    case ExprKind::Index: {
      const auto* ix = static_cast<const Index*>(e);
      std::string s = "(index " + dumpExpr(ix->object.get()) + " " + dumpExpr(ix->index.get());
      for (const auto& r : ix->rest) s += " " + dumpExpr(r.get());
      return s + ")";
    }
    case ExprKind::Member: {
      const auto* m = static_cast<const Member*>(e);
      return "(member " + dumpExpr(m->object.get()) + " " + m->name + ")";
    }
    case ExprKind::ListLit: {
      std::string s = "(list";
      for (const auto& el : static_cast<const ListLit*>(e)->elems) s += " " + dumpExpr(el.get());
      return s + ")";
    }
    case ExprKind::DictLit: {
      std::string s = "(dict";
      for (const auto& en : static_cast<const DictLit*>(e)->entries) {
        s += " (" + dumpExpr(en.first.get()) + " " + dumpExpr(en.second.get()) + ")";
      }
      return s + ")";
    }
    case ExprKind::Range: {
      const auto* r = static_cast<const RangeExpr*>(e);
      return "(range " + dumpExpr(r->start.get()) + " " + dumpExpr(r->end.get()) + ")";
    }
  }
  return "?";
}

// Añadir aquí los casos de nuevas StmtKind.
std::string dumpStmt(const Stmt* s) {
  switch (s->kind) {
    case StmtKind::VarDecl: {
      const auto* v = static_cast<const VarDecl*>(s);
      return std::string("(var ") + (v->isMut ? "mut " : "") + v->name + " " + dumpType(v->annot.get()) +
             " " + dumpExpr(v->init.get()) + ")";
    }
    case StmtKind::Assign: {
      const auto* a = static_cast<const Assign*>(s);
      return "(assign " + dumpExpr(a->target.get()) + " " + dumpExpr(a->value.get()) + ")";
    }
    case StmtKind::ExprStmt: return "(exprstmt " + dumpExpr(static_cast<const ExprStmt*>(s)->expr.get()) + ")";
    case StmtKind::Return: return "(return " + dumpExpr(static_cast<const ReturnStmt*>(s)->value.get()) + ")";
    case StmtKind::If: {
      const auto* i = static_cast<const IfStmt*>(s);
      return "(if " + dumpExpr(i->cond.get()) + " " + dumpBlock(i->thenBody) + " " + dumpBlock(i->elseBody) + ")";
    }
    case StmtKind::For: {
      const auto* f = static_cast<const ForStmt*>(s);
      return "(for " + f->var + " " + dumpExpr(f->iter.get()) + " " + dumpBlock(f->body) + ")";
    }
    case StmtKind::While: {
      const auto* w = static_cast<const WhileStmt*>(s);
      return "(while " + dumpExpr(w->cond.get()) + " " + dumpBlock(w->body) + ")";
    }
    case StmtKind::Break: return "(break)";
    case StmtKind::Continue: return "(continue)";
    case StmtKind::Fn: return "(localfn " + dumpDecl(static_cast<const FnStmt*>(s)->fn.get()) + ")";
  }
  return "?";
}

// Añadir aquí los casos de nuevas DeclKind.
std::string dumpDecl(const Decl* d) {
  switch (d->kind) {
    case DeclKind::Fn: {
      const auto* f = static_cast<const FnDecl*>(d);
      std::string s = "(fn " + f->name;
      s += f->isExtern ? " extern " + f->abi : std::string(" intern");
      s += f->hasBody ? " body" : " nobody";
      s += " (params";
      for (const Param& p : f->params) {
        s += std::string(" (") + (p.isSelf ? "self" : "param") + (p.selfMut ? " mut" : "") + " " + p.name + " " +
             dumpType(p.annot.get()) + ")";
      }
      s += ") ret " + dumpType(f->retAnnot.get()) + " " + dumpBlock(f->body) + ")";
      return s;
    }
    case DeclKind::Struct: {
      const auto* st = static_cast<const StructDecl*>(d);
      std::string s = "(struct " + st->name;
      for (const FieldDecl& f : st->fields) s += " (field " + f.name + " " + dumpType(f.annot.get()) + ")";
      return s + ")";
    }
    case DeclKind::Impl: {
      const auto* im = static_cast<const ImplDecl*>(d);
      std::string s = "(impl " + im->typeName;
      for (const auto& m : im->methods) s += " " + dumpDecl(m.get());
      return s + ")";
    }
    case DeclKind::Import: return "(import " + static_cast<const ImportDecl*>(d)->path + ")";
  }
  return "?";
}

}  // namespace

std::string dumpProgram(const Program& prog) {
  std::string s = "(program (decls";
  for (const auto& d : prog.decls) s += "\n  " + dumpDecl(d.get());
  s += ")\n (stmts";
  for (const auto& st : prog.stmts) s += "\n  " + dumpStmt(st.get());
  return s + "))\n";
}

}  // namespace lgfmt
