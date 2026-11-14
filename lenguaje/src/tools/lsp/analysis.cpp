#include "tools/lsp/analysis.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "parser/parser.h"
#include "sema/sema.h"

namespace fs = std::filesystem;

namespace lsp {
namespace {

const std::map<std::string, std::string> kBuiltinSig = {
    {"print", "print(...) -> void"},
    {"len", "len(x) -> int"},
    {"int", "int(x) -> int"},
    {"float", "float(x) -> float"},
    {"str", "str(x) -> str"},
    {"zeros", "zeros(d...) -> Tensor[float, forma]"},
    {"ones", "ones(d...) -> Tensor[float, forma]"},
    {"randn", "randn(d...) -> Tensor[float, forma]"},
    {"from_list", "from_list(lista, d...) -> Tensor"},
};

std::string canonicalPath(const std::string& file) {
  try {
    return fs::weakly_canonical(file).string();
  } catch (...) {
    return file;
  }
}

std::string typeStr(const lg::Type* t) { return t ? t->str() : "?"; }

std::string paramText(const lg::Param& p) {
  if (p.isSelf) return p.selfMut ? "mut self" : "self";
  return p.name + ": " + typeStr(p.type);
}

std::string fnSignature(const lg::FnDecl& f, const std::string& owner) {
  std::string out = f.isExtern ? "extern fn " : "fn ";
  if (!owner.empty()) out += owner + ".";
  out += f.name + "(";
  for (size_t i = 0; i < f.params.size(); ++i) {
    if (i) out += ", ";
    out += paramText(f.params[i]);
  }
  out += ")";
  if (f.retType && f.retType->kind != lg::TypeKind::Void) out += " -> " + f.retType->str();
  return out;
}

std::string structSignature(const lg::StructDecl& sd) {
  std::string out = "struct " + sd.name + "(";
  for (size_t i = 0; i < sd.fields.size(); ++i) {
    if (i) out += ", ";
    out += sd.fields[i].name + ": " + typeStr(sd.fields[i].type);
  }
  return out + ")";
}

// Carga el programa principal y sus imports (mismo orden que driver/loader.cpp).
// El archivo principal usa el texto del buffer en lugar del contenido en disco.
struct Loader {
  Snapshot& s;
  std::string mainFile, mainCanon, mainText;
  std::set<std::string> loaded, visiting;

  void load(const std::string& file, const lg::Loc* importedFrom) {
    const std::string canon = canonicalPath(file);
    if (loaded.count(canon)) return;
    if (visiting.count(canon)) {
      s.diags.error(importedFrom ? *importedFrom : lg::Loc{file, 1, 1},
                    "import circular: '" + file + "' forma un ciclo de imports",
                    "elimina uno de los imports del ciclo");
      return;
    }
    std::string text;
    if (canon == mainCanon) {
      text = mainText;
    } else {
      std::ifstream in(file, std::ios::binary);
      if (!in) {
        s.diags.error(importedFrom ? *importedFrom : lg::Loc{file, 1, 1},
                      "no se puede abrir el archivo '" + file + "'",
                      "comprueba la ruta y que el archivo exista");
        return;
      }
      std::stringstream ss;
      ss << in.rdbuf();
      text = ss.str();
    }

    lg::Diagnostics scratch;  // los errores léxicos ya los reporta parseProgram
    FileText& ft = s.files[file];
    ft.text = text;
    ft.tokens = lg::tokenize(file, text, scratch);

    visiting.insert(canon);
    auto prog = lg::parseProgram(file, text, s.diags);
    const fs::path dir = fs::path(file).parent_path();
    std::vector<std::unique_ptr<lg::Decl>> kept;
    for (auto& d : prog->decls) {
      if (d->kind == lg::DeclKind::Import) {
        auto* imp = static_cast<lg::ImportDecl*>(d.get());
        load((dir / (imp->path + ".lg")).string(), &imp->loc);
      } else {
        kept.push_back(std::move(d));
      }
    }
    for (auto& d : kept) s.prog->decls.push_back(std::move(d));
    for (auto& st : prog->stmts) s.prog->stmts.push_back(std::move(st));
    visiting.erase(canon);
    loaded.insert(canon);
  }
};

// Recorre el AST anotado y rellena defs/refs. Resuelve ámbitos igual que sema:
// las funciones no ven variables de nivel superior.
class Indexer {
public:
  explicit Indexer(Snapshot& s) : s_(s) {}

  void run() {
    lg::Program& p = *s_.prog;
    scopes_.emplace_back();  // ámbito de sentencias de nivel superior

    for (auto& d : p.decls) {
      if (!d) continue;
      if (d->kind == lg::DeclKind::Struct) {
        declareStruct(static_cast<lg::StructDecl&>(*d));
      } else if (d->kind == lg::DeclKind::Fn) {
        declareFn(static_cast<lg::FnDecl&>(*d), "");
      } else if (d->kind == lg::DeclKind::Impl) {
        auto& im = static_cast<lg::ImplDecl&>(*d);
        for (auto& m : im.methods) {
          if (m) declareFn(*m, im.typeName);
        }
      }
    }

    for (auto& d : p.decls) {
      if (!d) continue;
      if (d->kind == lg::DeclKind::Fn) {
        walkFn(static_cast<lg::FnDecl&>(*d));
      } else if (d->kind == lg::DeclKind::Impl) {
        for (auto& m : static_cast<lg::ImplDecl&>(*d).methods) {
          if (m) walkFn(*m);
        }
      }
    }
    for (auto& st : p.stmts) walkStmt(st.get());
  }

private:
  Snapshot& s_;
  std::vector<std::map<std::string, int>> scopes_;
  size_t base_ = 0;  // primer ámbito visible (las funciones no ven el ámbito de nivel superior)
  std::map<std::string, int> globalFns_, globalStructs_, builtins_;
  std::map<std::string, int> methodByMangled_;  // mangled -> def
  std::map<std::string, int> memberByKey_;      // "Struct.nombre" -> def (campo o método)

  // ---------- utilidades ----------
  // Primer token Ident en o tras (line, col) del archivo; si no hay, la propia posición.
  lg::Loc nameAfter(const std::string& file, int line, int col) const {
    auto it = s_.files.find(file);
    if (it == s_.files.end()) return lg::Loc{file, line, col};
    const auto& toks = it->second.tokens;
    auto pos = std::lower_bound(toks.begin(), toks.end(), std::make_pair(line, col),
                                [](const lg::Token& t, const std::pair<int, int>& p) {
                                  return std::make_pair(t.line, t.col) < p;
                                });
    for (; pos != toks.end(); ++pos) {
      if (pos->kind == lg::TokenKind::Ident) return lg::Loc{file, pos->line, pos->col};
    }
    return lg::Loc{file, line, col};
  }

  int addDef(Def d) {
    s_.defs.push_back(std::move(d));
    return static_cast<int>(s_.defs.size()) - 1;
  }

  void addRef(const std::string& file, int line, int col, const std::string& name, int def) {
    Ref r;
    r.file = file;
    r.line = line;
    r.col = col;
    r.len = static_cast<int>(name.size());
    r.name = name;
    r.def = def;
    const std::string key = file + ":" + std::to_string(line) + ":" + std::to_string(col);
    s_.identRefs[key] = static_cast<int>(s_.refs.size());
    s_.refs.push_back(std::move(r));
  }

  int builtinDef(const std::string& name) {
    auto it = builtins_.find(name);
    if (it != builtins_.end()) return it->second;
    Def d;
    d.kind = Def::Kind::Builtin;
    d.name = name;
    auto sig = kBuiltinSig.find(name);
    d.detail = "builtin " + (sig != kBuiltinSig.end() ? sig->second : name);
    int idx = addDef(std::move(d));
    builtins_[name] = idx;
    return idx;
  }

  int resolve(const std::string& name) {
    for (int i = static_cast<int>(scopes_.size()) - 1; i >= static_cast<int>(base_); --i) {
      auto it = scopes_[i].find(name);
      if (it != scopes_[i].end()) return it->second;
    }
    if (auto it = globalFns_.find(name); it != globalFns_.end()) return it->second;
    if (auto it = globalStructs_.find(name); it != globalStructs_.end()) return it->second;
    static const std::set<std::string> kBuiltinNames = {"print", "len", "int", "float", "str",
                                                        "zeros", "ones", "randn", "from_list"};
    if (kBuiltinNames.count(name)) return builtinDef(name);
    return -1;
  }

  // ---------- declaraciones de nivel superior ----------
  void declareStruct(const lg::StructDecl& sd) {
    Def d;
    d.kind = Def::Kind::Struct;
    d.name = sd.name;
    const lg::Loc nl = nameAfter(sd.loc.file, sd.loc.line, sd.loc.col);
    d.file = nl.file;
    d.line = nl.line;
    d.col = nl.col;
    d.detail = structSignature(sd);
    const int idx = addDef(std::move(d));
    globalStructs_[sd.name] = idx;
    addRef(nl.file, nl.line, nl.col, sd.name, idx);

    for (const auto& f : sd.fields) {
      Def fd;
      fd.kind = Def::Kind::Field;
      fd.name = f.name;
      fd.file = f.loc.file;
      fd.line = f.loc.line;
      fd.col = f.loc.col;
      fd.type = f.type;
      fd.owner = sd.name;
      fd.detail = "campo " + f.name + ": " + typeStr(f.type);
      const int fi = addDef(std::move(fd));
      memberByKey_[sd.name + "." + f.name] = fi;
      s_.membersOf[sd.name].push_back(fi);
      addRef(f.loc.file, f.loc.line, f.loc.col, f.name, fi);
    }
  }

  void declareFn(const lg::FnDecl& f, const std::string& owner) {
    Def d;
    d.kind = owner.empty() ? Def::Kind::Function : Def::Kind::Method;
    d.name = f.name;
    d.owner = owner;
    const lg::Loc nl = nameAfter(f.loc.file, f.loc.line, f.loc.col);
    d.file = nl.file;
    d.line = nl.line;
    d.col = nl.col;
    d.detail = fnSignature(f, owner);
    const int idx = addDef(std::move(d));
    if (owner.empty()) {
      globalFns_[f.name] = idx;
    } else {
      if (!f.mangled.empty()) methodByMangled_[f.mangled] = idx;
      memberByKey_[owner + "." + f.name] = idx;
      s_.membersOf[owner].push_back(idx);
    }
    addRef(nl.file, nl.line, nl.col, f.name, idx);
  }

  // ---------- cuerpos ----------
  void walkFn(const lg::FnDecl& f) {
    const size_t savedBase = base_;
    base_ = scopes_.size();
    scopes_.emplace_back();
    for (const auto& p : f.params) {
      Def d;
      d.kind = Def::Kind::Parameter;
      d.name = p.name;
      lg::Loc nl = p.isSelf ? nameAfter(p.loc.file, p.loc.line, p.loc.col) : p.loc;
      d.file = nl.file;
      d.line = nl.line;
      d.col = nl.col;
      d.type = p.type;
      if (p.isSelf) {
        d.detail = std::string(p.selfMut ? "mut " : "") + "self: " + typeStr(p.type);
      } else {
        d.detail = p.name + ": " + typeStr(p.type);
      }
      const int idx = addDef(std::move(d));
      scopes_.back()[p.name] = idx;
      addRef(nl.file, nl.line, nl.col, p.name, idx);
    }
    if (f.hasBody) walkBlock(f.body);
    scopes_.pop_back();
    base_ = savedBase;
  }

  void walkBlock(const std::vector<lg::StmtPtr>& body) {
    scopes_.emplace_back();
    for (const auto& st : body) walkStmt(st.get());
    scopes_.pop_back();
  }

  void defineVar(const std::string& name, const lg::Loc& nl, bool isMut, const lg::Type* type) {
    Def d;
    d.kind = Def::Kind::Variable;
    d.name = name;
    d.file = nl.file;
    d.line = nl.line;
    d.col = nl.col;
    d.type = type;
    d.detail = std::string(isMut ? "mut " : "") + name + ": " + typeStr(type);
    const int idx = addDef(std::move(d));
    scopes_.back()[name] = idx;
    addRef(nl.file, nl.line, nl.col, name, idx);
  }

  void walkStmt(const lg::Stmt* st) {
    if (!st) return;
    switch (st->kind) {
      case lg::StmtKind::VarDecl: {
        auto* v = static_cast<const lg::VarDecl*>(st);
        walkExpr(v->init.get());
        defineVar(v->name, nameAfter(v->loc.file, v->loc.line, v->loc.col), v->isMut, v->type);
        break;
      }
      case lg::StmtKind::Assign: {
        auto* a = static_cast<const lg::Assign*>(st);
        walkExpr(a->value.get());
        const lg::Expr* t = a->target.get();
        if (a->declares && t && t->kind == lg::ExprKind::Ident) {
          auto* id = static_cast<const lg::Ident*>(t);
          const lg::Type* ty = t->type ? t->type : (a->value ? a->value->type : nullptr);
          defineVar(id->name, id->loc, false, ty);
        } else {
          walkExpr(t);
        }
        break;
      }
      case lg::StmtKind::ExprStmt:
        walkExpr(static_cast<const lg::ExprStmt*>(st)->expr.get());
        break;
      case lg::StmtKind::Return:
        walkExpr(static_cast<const lg::ReturnStmt*>(st)->value.get());
        break;
      case lg::StmtKind::If: {
        auto* i = static_cast<const lg::IfStmt*>(st);
        walkExpr(i->cond.get());
        walkBlock(i->thenBody);
        walkBlock(i->elseBody);
        break;
      }
      case lg::StmtKind::For: {
        auto* f = static_cast<const lg::ForStmt*>(st);
        walkExpr(f->iter.get());
        scopes_.emplace_back();
        defineVar(f->var, nameAfter(f->loc.file, f->loc.line, f->loc.col), false, f->varType);
        walkBlock(f->body);
        scopes_.pop_back();
        break;
      }
      case lg::StmtKind::While: {
        auto* w = static_cast<const lg::WhileStmt*>(st);
        walkExpr(w->cond.get());
        walkBlock(w->body);
        break;
      }
      case lg::StmtKind::Break:
      case lg::StmtKind::Continue:
        break;
    }
  }

  void recordMember(const lg::Member& m, lg::CallKind ck, const std::string& target) {
    Ref r;
    r.file = m.loc.file;
    r.member = true;
    r.objLine = m.loc.line;
    r.objCol = m.loc.col;
    r.name = m.name;
    r.len = static_cast<int>(m.name.size());
    r.objType = m.object ? m.object->type : nullptr;
    if (ck == lg::CallKind::Method) {
      auto it = methodByMangled_.find(target);
      if (it != methodByMangled_.end()) r.def = it->second;
    } else if (ck == lg::CallKind::BuiltinMethod) {
      r.detail = "método builtin " + target;
    }
    if (r.def < 0) {
      const std::string owner = structNameOf(r.objType);
      if (!owner.empty()) {
        auto it = memberByKey_.find(owner + "." + m.name);
        if (it != memberByKey_.end()) r.def = it->second;
      }
    }
    s_.refs.push_back(std::move(r));
  }

  void walkExpr(const lg::Expr* e) {
    if (!e) return;
    switch (e->kind) {
      case lg::ExprKind::Ident: {
        auto* id = static_cast<const lg::Ident*>(e);
        addRef(id->loc.file, id->loc.line, id->loc.col, id->name, resolve(id->name));
        break;
      }
      case lg::ExprKind::Unary:
        walkExpr(static_cast<const lg::Unary*>(e)->operand.get());
        break;
      case lg::ExprKind::Binary: {
        auto* b = static_cast<const lg::Binary*>(e);
        walkExpr(b->lhs.get());
        walkExpr(b->rhs.get());
        break;
      }
      case lg::ExprKind::Call: {
        auto* c = static_cast<const lg::Call*>(e);
        if (c->callee && c->callee->kind == lg::ExprKind::Member) {
          auto* m = static_cast<const lg::Member*>(c->callee.get());
          walkExpr(m->object.get());
          recordMember(*m, c->callKind, c->target);
        } else {
          walkExpr(c->callee.get());
        }
        for (const auto& a : c->args) walkExpr(a.get());
        break;
      }
      case lg::ExprKind::Index: {
        auto* ix = static_cast<const lg::Index*>(e);
        walkExpr(ix->object.get());
        walkExpr(ix->index.get());
        for (const auto& r : ix->rest) walkExpr(r.get());
        break;
      }
      case lg::ExprKind::Member: {
        auto* m = static_cast<const lg::Member*>(e);
        walkExpr(m->object.get());
        recordMember(*m, lg::CallKind::Unresolved, "");
        break;
      }
      case lg::ExprKind::ListLit:
        for (const auto& el : static_cast<const lg::ListLit*>(e)->elems) walkExpr(el.get());
        break;
      case lg::ExprKind::DictLit:
        for (const auto& kv : static_cast<const lg::DictLit*>(e)->entries) {
          walkExpr(kv.first.get());
          walkExpr(kv.second.get());
        }
        break;
      case lg::ExprKind::Range: {
        auto* r = static_cast<const lg::RangeExpr*>(e);
        walkExpr(r->start.get());
        walkExpr(r->end.get());
        break;
      }
      case lg::ExprKind::IntLit:
      case lg::ExprKind::FloatLit:
      case lg::ExprKind::BoolLit:
      case lg::ExprKind::StrLit:
        break;
    }
  }
};

}  // namespace

std::string structNameOf(const lg::Type* t) {
  return (t && t->kind == lg::TypeKind::Struct) ? t->name : std::string();
}

std::unique_ptr<Snapshot> buildSnapshot(const std::string& mainFile, const std::string& mainText) {
  auto s = std::make_unique<Snapshot>();
  s->mainFile = mainFile;
  s->prog = std::make_unique<lg::Program>();
  s->prog->file = mainFile;

  Loader ld{*s, mainFile, canonicalPath(mainFile), mainText, {}, {}};
  ld.load(mainFile, nullptr);

  s->parsed = !s->diags.hasErrors();
  if (!s->parsed) return s;  // como el driver: sin sema si hay errores de sintaxis o de carga

  s->analyzed = lg::analyze(*s->prog, s->types, s->diags) && !s->diags.hasErrors();
  Indexer(*s).run();
  return s;
}

const Ref* identRefAt(const Snapshot& s, const std::string& file, int line, int col) {
  auto it = s.identRefs.find(file + ":" + std::to_string(line) + ":" + std::to_string(col));
  return it == s.identRefs.end() ? nullptr : &s.refs[it->second];
}

const Ref* memberRefAt(const Snapshot& s, const std::string& file, const std::string& name, int line, int col) {
  // Miembros: se identifican por la expresión del objeto. Si hay varios en la misma línea,
  // gana el de objeto más cercano a la izquierda del cursor.
  const Ref* best = nullptr;
  for (const auto& r : s.refs) {
    if (!r.member || r.file != file || r.name != name) continue;
    if (r.objLine != line || r.objCol > col) continue;
    if (!best || r.objCol >= best->objCol) best = &r;
  }
  return best;
}

const std::vector<std::string>& keywordNames() {
  static const std::vector<std::string> k = {
      "fn", "mut", "if", "elif", "else", "for", "in", "while", "return", "struct", "impl",
      "extern", "import", "true", "false", "break", "continue", "and", "or", "not"};
  return k;
}

const std::vector<std::string>& typeNames() {
  static const std::vector<std::string> t = {"int", "float", "bool", "str", "List", "Dict", "Tensor"};
  return t;
}

const std::vector<std::string>& builtinNames() {
  static const std::vector<std::string> b = {"print", "len", "int", "float", "str",
                                             "zeros", "ones", "randn", "from_list"};
  return b;
}

}  // namespace lsp
