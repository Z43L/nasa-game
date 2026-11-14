#include "driver/loader.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "parser/parser.h"

namespace fs = std::filesystem;

namespace lg {
namespace {

// Divide LG_PATH por ':' ignorando entradas vacías.
std::vector<std::string> envSearchPaths() {
  std::vector<std::string> out;
  const char* env = std::getenv("LG_PATH");
  if (!env) return out;
  std::string cur;
  for (const char* p = env;; ++p) {
    if (*p == ':' || *p == '\0') {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
      if (*p == '\0') break;
    } else {
      cur += *p;
    }
  }
  return out;
}

// Último segmento de una ruta de import: "lib/datos/json" -> "json".
std::string lastSegment(const std::string& path) {
  auto p = path.rfind('/');
  return p == std::string::npos ? path : path.substr(p + 1);
}

// Un archivo cargado. Tras la carga, sus fn y struct (no extern) reciben un nombre interno
// "alias__nombre" para que dos módulos puedan definir el mismo nombre.
struct ModRec {
  std::string file;
  std::string alias;  // último segmento de su ruta de import ("" para el programa principal)
  bool isMain = false;
  std::string prefix;  // prefijo interno: "json__", o "json2__" si otro módulo ya usa "json__"
  std::vector<std::unique_ptr<Decl>> decls;  // sin los ImportDecl
  std::vector<StmtPtr> stmts;
  // Nombres que este archivo define (fn, struct y extern) -> nombre interno.
  std::map<std::string, std::string> pub;
  struct Imp {
    std::string alias;
    int mod;
    Loc loc;
  };
  std::vector<Imp> imports;  // imports directos, ya resueltos
};

// Reescribe los nombres de un módulo: alias.nombre, Alias.Tipo y nombres sin cualificar
// (propios, de sus imports directos o, si son únicos en el programa, de cualquier módulo).
// Respeta las variables locales, que ocultan cualquier nombre global.
class Renamer {
 public:
  Renamer(Diagnostics& diags, std::vector<ModRec>& mods, int self,
          const std::map<std::string, std::set<int>>& global)
      : diags_(diags), mods_(mods), self_(self), global_(global) {
    for (auto& imp : mods_[self].imports) {
      auto it = aliasEnv_.find(imp.alias);
      if (it != aliasEnv_.end() && it->second != imp.mod) {
        diags_.error(imp.loc,
                     "dos imports de este archivo usan el alias '" + imp.alias + "' para módulos distintos",
                     "cada import necesita un nombre distinto: el alias es el último segmento de la ruta "
                     "(import a/x y import b/x chocan)");
        continue;
      }
      aliasEnv_[imp.alias] = imp.mod;
      for (auto& kv : mods_[imp.mod].pub) direct_[kv.first].insert(imp.mod);
    }
  }

  void run() {
    ModRec& m = mods_[self_];
    for (auto& d : m.decls) {
      switch (d->kind) {
        case DeclKind::Fn:
          fnDecl(static_cast<FnDecl&>(*d));
          break;
        case DeclKind::Struct:
          for (auto& f : static_cast<StructDecl&>(*d).fields) type(f.annot);
          break;
        case DeclKind::Impl: {
          auto& im = static_cast<ImplDecl&>(*d);
          im.typeName = resolveName(im.typeName, im.typeLoc);
          for (auto& meth : im.methods) fnDecl(*meth);
          break;
        }
        case DeclKind::Import:
          break;
      }
    }
    // Sentencias de nivel superior: sus variables son locales al archivo.
    scopes_.clear();
    scopes_.emplace_back();
    stmtList(m.stmts);
    scopes_.clear();
  }

 private:
  Diagnostics& diags_;
  std::vector<ModRec>& mods_;
  int self_;
  const std::map<std::string, std::set<int>>& global_;
  std::map<std::string, int> aliasEnv_;               // alias -> módulo
  std::map<std::string, std::set<int>> direct_;       // nombre -> módulos de imports directos que lo definen
  std::vector<std::set<std::string>> scopes_;         // variables locales

  bool isLocal(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it)
      if (it->count(name)) return true;
    return false;
  }

  void declare(const std::string& name) {
    if (!scopes_.empty()) scopes_.back().insert(name);
  }

  std::string displayName(int mod) const {
    const ModRec& m = mods_[mod];
    return m.isMain ? std::string("el programa principal") : "'" + m.alias + "'";
  }

  // Elige el nombre interno de `name` entre los módulos `provs`. Si son varios, error.
  std::string pick(const std::set<int>& provs, const std::string& name, const Loc& loc) {
    if (provs.size() == 1) return mods_[*provs.begin()].pub.at(name);
    std::string list, hintAlias;
    for (int p : provs) {
      if (!list.empty()) list += ", ";
      list += displayName(p);
      if (hintAlias.empty() && !mods_[p].isMain) hintAlias = mods_[p].alias;
    }
    std::string hint = hintAlias.empty()
                           ? "renombra una de las definiciones"
                           : "usa el nombre cualificado con el alias del módulo, por ejemplo: " + hintAlias + "." + name + "(...)";
    diags_.error(loc, "el nombre '" + name + "' es ambiguo: lo definen " + list, hint);
    return name;
  }

  // Nombre interno de un nombre sin cualificar (no local).
  std::string resolveName(const std::string& name, const Loc& loc) {
    auto own = mods_[self_].pub.find(name);
    if (own != mods_[self_].pub.end()) return own->second;
    auto d = direct_.find(name);
    if (d != direct_.end()) return pick(d->second, name, loc);
    auto g = global_.find(name);
    if (g != global_.end()) return pick(g->second, name, loc);
    return name;  // builtin, o error que reportará sema
  }

  void fnDecl(FnDecl& f) {
    scopes_.emplace_back();
    for (auto& p : f.params) {
      declare(p.name);
      type(p.annot);
    }
    type(f.retAnnot);
    stmtList(f.body);
    scopes_.pop_back();
  }

  void type(TypeExprPtr& t) {
    if (!t) return;
    if (t->kind == TypeExpr::Kind::Name) {
      auto dot = t->name.find('.');
      if (dot == std::string::npos) {
        t->name = resolveName(t->name, t->loc);
        return;
      }
      const std::string alias = t->name.substr(0, dot);
      const std::string member = t->name.substr(dot + 1);
      auto it = aliasEnv_.find(alias);
      if (it == aliasEnv_.end()) {
        diags_.error(t->loc, "'" + alias + "' no es un módulo importado",
                     "importa el módulo con: import " + alias);
        return;
      }
      auto p = mods_[it->second].pub.find(member);
      if (p == mods_[it->second].pub.end()) {
        diags_.error(t->loc, "el módulo '" + alias + "' no define '" + member + "'",
                     "comprueba el nombre del tipo en el archivo del módulo");
        return;
      }
      t->name = p->second;
      return;
    }
    for (auto& a : t->args) type(a);
    type(t->ret);
  }

  void stmtList(std::vector<StmtPtr>& body) {
    for (auto& s : body) stmt(*s);
  }

  void block(std::vector<StmtPtr>& body) {
    scopes_.emplace_back();
    stmtList(body);
    scopes_.pop_back();
  }

  void stmt(Stmt& s) {
    switch (s.kind) {
      case StmtKind::VarDecl: {
        auto& v = static_cast<VarDecl&>(s);
        type(v.annot);
        expr(v.init);
        declare(v.name);
        return;
      }
      case StmtKind::Assign: {
        auto& a = static_cast<Assign&>(s);
        expr(a.value);
        if (a.target->kind == ExprKind::Ident) {
          auto& id = static_cast<Ident&>(*a.target);
          if (!isLocal(id.name)) declare(id.name);  // sema lo declara aquí si no es visible
        } else {
          expr(a.target);
        }
        return;
      }
      case StmtKind::ExprStmt:
        expr(static_cast<ExprStmt&>(s).expr);
        return;
      case StmtKind::Return:
        expr(static_cast<ReturnStmt&>(s).value);
        return;
      case StmtKind::If: {
        auto& i = static_cast<IfStmt&>(s);
        expr(i.cond);
        block(i.thenBody);
        block(i.elseBody);
        return;
      }
      case StmtKind::For: {
        auto& f = static_cast<ForStmt&>(s);
        expr(f.iter);
        scopes_.emplace_back();
        declare(f.var);
        stmtList(f.body);
        scopes_.pop_back();
        return;
      }
      case StmtKind::While: {
        auto& w = static_cast<WhileStmt&>(s);
        expr(w.cond);
        block(w.body);
        return;
      }
      case StmtKind::Break:
      case StmtKind::Continue:
        return;
      case StmtKind::Fn: {
        auto& fs = static_cast<FnStmt&>(s);
        declare(fs.fn->name);
        fnDecl(*fs.fn);  // ve las variables del ámbito externo (clausuras)
        return;
      }
    }
  }

  void expr(ExprPtr& e) {
    if (!e) return;
    switch (e->kind) {
      case ExprKind::IntLit:
      case ExprKind::FloatLit:
      case ExprKind::BoolLit:
      case ExprKind::StrLit:
      case ExprKind::NoneLit:
        return;
      case ExprKind::Ident: {
        auto& id = static_cast<Ident&>(*e);
        if (!isLocal(id.name)) id.name = resolveName(id.name, id.loc);
        return;
      }
      case ExprKind::Unary:
        expr(static_cast<Unary&>(*e).operand);
        return;
      case ExprKind::Binary: {
        auto& b = static_cast<Binary&>(*e);
        expr(b.lhs);
        expr(b.rhs);
        return;
      }
      case ExprKind::Call: {
        auto& c = static_cast<Call&>(*e);
        expr(c.callee);
        for (auto& a : c.args) expr(a);
        for (auto& t : c.typeArgs) type(t);
        return;
      }
      case ExprKind::Index: {
        auto& ix = static_cast<Index&>(*e);
        expr(ix.object);
        expr(ix.index);
        for (auto& r : ix.rest) expr(r);
        return;
      }
      case ExprKind::Member: {
        auto& m = static_cast<Member&>(*e);
        if (m.object->kind == ExprKind::Ident) {
          const auto& objName = static_cast<Ident&>(*m.object).name;
          auto al = aliasEnv_.find(objName);
          if (!isLocal(objName) && al != aliasEnv_.end()) {
            const ModRec& target = mods_[al->second];
            auto p = target.pub.find(m.name);
            if (p == target.pub.end()) {
              diags_.error(m.loc, "el módulo '" + objName + "' no define '" + m.name + "'",
                           "comprueba el nombre; un módulo exporta sus fn y struct de nivel superior");
              return;
            }
            e = std::make_unique<Ident>(m.loc, p->second);  // alias.nombre -> nombre interno
            return;
          }
        }
        expr(m.object);
        return;
      }
      case ExprKind::ListLit:
        for (auto& el : static_cast<ListLit&>(*e).elems) expr(el);
        return;
      case ExprKind::DictLit:
        for (auto& kv : static_cast<DictLit&>(*e).entries) {
          expr(kv.first);
          expr(kv.second);
        }
        return;
      case ExprKind::Range: {
        auto& r = static_cast<RangeExpr&>(*e);
        expr(r.start);
        expr(r.end);
        return;
      }
      case ExprKind::Await:
        expr(static_cast<AwaitExpr&>(*e).operand);
        return;
    }
  }
};

struct Loader {
  Diagnostics& diags;
  const LoadOptions& opts;
  std::vector<ModRec> mods;        // en orden de dependencia (los importados antes que su importador)
  std::map<std::string, int> loaded;  // ruta canónica -> índice en mods
  std::set<std::string> visiting;  // pila para detectar ciclos
  bool ok = true;

  // Busca el módulo de `imp` según el orden documentado en loader.h.
  // Devuelve la ruta del archivo o "" (en ese caso registra el error).
  std::string resolve(const ImportDecl& imp, const fs::path& importerDir) {
    const std::string rel = imp.path + ".lg";
    std::vector<std::string> roots;
    roots.push_back(importerDir.string());
    for (auto& p : envSearchPaths()) roots.push_back(p);
    for (auto& p : opts.searchPaths) roots.push_back(p);

    std::vector<std::string> candidates;
    for (auto& root : roots) {
      fs::path cand = fs::path(root) / rel;
      std::error_code ec;
      if (fs::is_regular_file(cand, ec)) return cand.string();
      candidates.push_back(cand.string());
    }

    std::string msg = "no se puede abrir el módulo '" + imp.path + "': no se encuentra en ninguna ruta de búsqueda. Rutas buscadas:";
    for (auto& c : candidates) msg += "\n    " + c;
    diags.error(imp.loc, msg,
                "crea '" + rel + "' en una de esas carpetas, añade su carpeta con LG_PATH o "
                "'lgc -I carpeta', o declara la dependencia en lg.toml con 'lgpm añadir'");
    ok = false;
    return "";
  }

  // Carga `file` y devuelve su índice en mods, o -1 si no se pudo cargar.
  int load(const std::string& file, const Loc* importedFrom, const std::string& alias, bool isMain) {
    std::string canon = fs::weakly_canonical(file).string();
    auto done = loaded.find(canon);
    if (done != loaded.end()) return done->second;
    if (visiting.count(canon)) {
      diags.error(importedFrom ? *importedFrom : Loc{file, 1, 1},
                  "import circular: '" + file + "' forma un ciclo de imports",
                  "elimina uno de los imports del ciclo (un módulo no puede importarse directa ni indirectamente a sí mismo)");
      ok = false;
      return -1;
    }

    std::ifstream in(file, std::ios::binary);
    if (!in) {
      Loc loc = importedFrom ? *importedFrom : Loc{file, 1, 1};
      diags.error(loc, "no se puede abrir el archivo '" + file + "'",
                  "comprueba la ruta y que el archivo exista");
      ok = false;
      return -1;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string source = ss.str();
    diags.registerSource(file, source);

    visiting.insert(canon);
    auto prog = parseProgram(file, source, diags);
    if (diags.hasErrors()) ok = false;

    ModRec rec;
    rec.file = file;
    rec.alias = alias;
    rec.isMain = isMain;
    fs::path dir = fs::path(file).parent_path();
    // Primero los imports de este módulo (dependencias).
    for (auto& d : prog->decls) {
      if (d->kind == DeclKind::Import) {
        auto* imp = static_cast<ImportDecl*>(d.get());
        std::string target = resolve(*imp, dir);
        if (target.empty()) continue;
        std::string al = lastSegment(imp->path);
        int idx = load(target, &imp->loc, al, false);
        if (idx >= 0) rec.imports.push_back({al, idx, imp->loc});
      } else {
        rec.decls.push_back(std::move(d));
      }
    }
    rec.stmts = std::move(prog->stmts);

    visiting.erase(canon);
    mods.push_back(std::move(rec));
    int idx = static_cast<int>(mods.size()) - 1;
    loaded[canon] = idx;
    return idx;
  }

  // Asigna los nombres internos, resuelve las referencias y pasa las declaraciones a `out`.
  void finalize(Program& out) {
    std::set<std::string> prefixes;
    for (auto& m : mods) {
      if (!m.isMain) {
        std::string p = m.alias + "__";
        for (int k = 2; prefixes.count(p); ++k) p = m.alias + std::to_string(k) + "__";
        prefixes.insert(p);
        m.prefix = p;
      }
      auto internal = [&](const std::string& name, bool isExtern) {
        return (m.isMain || isExtern) ? name : m.prefix + name;
      };
      for (auto& d : m.decls) {
        if (d->kind == DeclKind::Fn) {
          auto* fd = static_cast<FnDecl*>(d.get());
          std::string orig = fd->name;
          fd->name = internal(orig, fd->isExtern);  // extern "C" conserva su símbolo C
          m.pub[orig] = fd->name;
        } else if (d->kind == DeclKind::Struct) {
          auto* sd = static_cast<StructDecl*>(d.get());
          std::string orig = sd->name;
          sd->name = internal(orig, false);
          m.pub[orig] = sd->name;
        }
      }
    }

    if (!diags.hasErrors()) {
      std::map<std::string, std::set<int>> global;
      for (int i = 0; i < static_cast<int>(mods.size()); ++i)
        for (auto& kv : mods[i].pub) global[kv.first].insert(i);
      for (int i = 0; i < static_cast<int>(mods.size()); ++i) {
        Renamer r(diags, mods, i, global);
        r.run();
      }
    }

    for (auto& m : mods) {
      for (auto& d : m.decls) out.decls.push_back(std::move(d));
      for (auto& s : m.stmts) out.stmts.push_back(std::move(s));
    }
  }
};

}  // namespace

bool loadProgram(const std::string& mainFile, Program& out, Diagnostics& diags,
                 const LoadOptions& opts) {
  out.file = mainFile;
  Loader l{diags, opts, {}, {}, {}, true};
  l.load(mainFile, nullptr, "", true);
  l.finalize(out);
  return l.ok && !diags.hasErrors();
}

}  // namespace lg
