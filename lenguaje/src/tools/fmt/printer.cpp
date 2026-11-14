// Impresor canónico del AST (lgfmt).
//
// DÓNDE AÑADIR CASOS NUEVOS: cuando se añada un ExprKind, StmtKind o DeclKind en src/ast/ast.h,
// añade su caso en el switch correspondiente de este archivo (exprText, stmt, decl) y en
// ast_dump.cpp (dumpExpr, dumpStmt, dumpDecl). Si la nueva expresión tiene precedencia propia,
// actualiza precOf y binInfo. Los switches no tienen `default`: -Wswitch avisa de los casos que falten.
//
// Reglas de formato: indentación de 4 espacios; un espacio alrededor de operadores binarios;
// espacio tras ':' y ',' en listas, llamadas, diccionarios y parámetros; sin espacio antes de ':'
// de bloques; una línea en blanco entre declaraciones de nivel superior; líneas en blanco entre
// sentencias de un bloque solo si el original las tenía (máximo una).
//
// Paréntesis: el AST no los guarda, así que se regeneran según la precedencia del parser
// (parseOr < parseAnd < parseNot < parseComparison < parseAdditive < parseTerm < parseUnary < parsePostfix).
//
// Comentarios (ver comments.h): los de línea completa se emiten antes del siguiente elemento
// del AST con su misma indentación, o al final de un bloque si su columna es >= la del bloque
// y están antes del siguiente elemento de la escala exterior. Los comentarios de cola se emiten
// en la línea de la cabecera de la sentencia que empieza en su línea.
#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "comments.h"
#include "format.h"

namespace lgfmt {
namespace {

using namespace lg;

// Precedencia de menor a mayor. Debe coincidir con las funciones parseXxx de parser.cpp.
enum Prec : int {
  kLowest = 0,  // rangos a..b (solo en la cabecera de for)
  kOr = 1,
  kAnd = 2,
  kNot = 3,
  kCmp = 4,
  kAdd = 5,
  kMul = 6,
  kNeg = 7,
  kPostfix = 8,  // llamada, índice, campo y método
  kAtom = 9      // literales, identificadores, listas y diccionarios
};

struct BinInfo {
  int prec;
  const char* text;
};

BinInfo binInfo(BinaryOp op) {
  switch (op) {
    case BinaryOp::Or: return {kOr, "or"};
    case BinaryOp::And: return {kAnd, "and"};
    case BinaryOp::Eq: return {kCmp, "=="};
    case BinaryOp::Ne: return {kCmp, "!="};
    case BinaryOp::Lt: return {kCmp, "<"};
    case BinaryOp::Gt: return {kCmp, ">"};
    case BinaryOp::Le: return {kCmp, "<="};
    case BinaryOp::Ge: return {kCmp, ">="};
    case BinaryOp::In: return {kCmp, "in"};
    case BinaryOp::Add: return {kAdd, "+"};
    case BinaryOp::Sub: return {kAdd, "-"};
    case BinaryOp::Mul: return {kMul, "*"};
    case BinaryOp::Div: return {kMul, "/"};
    case BinaryOp::Mod: return {kMul, "%"};
    case BinaryOp::MatMul: return {kMul, "@"};
  }
  return {kLowest, "?"};
}

int precOf(const Expr& e) {
  switch (e.kind) {
    case ExprKind::Binary: return binInfo(static_cast<const Binary&>(e).op).prec;
    case ExprKind::Unary:
      return static_cast<const Unary&>(e).op == UnaryOp::Neg ? kNeg : kNot;
    case ExprKind::Call: return static_cast<const Call&>(e).spawn ? kNeg : kPostfix;  // hilo f(x) es prefijo
    case ExprKind::Await: return kNeg;
    case ExprKind::Index:
    case ExprKind::Member: return kPostfix;
    case ExprKind::Range: return kLowest;
    case ExprKind::IntLit:
    case ExprKind::FloatLit:
    case ExprKind::BoolLit:
    case ExprKind::StrLit:
    case ExprKind::NoneLit:
    case ExprKind::Ident:
    case ExprKind::ListLit:
    case ExprKind::DictLit: return kAtom;
  }
  return kAtom;
}

// Número con la forma más corta que vuelve a leer el mismo valor. Siempre lleva '.' o 'e',
// para que el lexer lo trate como float y no como int.
std::string formatFloat(double v) {
  if (std::isinf(v)) return v > 0 ? "1e400" : "-1e400";  // desbordamiento de un literal válido
  char buf[64];
  const double a = std::fabs(v);
  if (a == 0 || (a >= 1e-4 && a < 1e15)) {
    for (int d = 1; d <= 20; ++d) {
      std::snprintf(buf, sizeof buf, "%.*f", d, v);
      if (std::strtod(buf, nullptr) == v) return buf;
    }
  }
  for (int p = 1; p <= 17; ++p) {
    std::snprintf(buf, sizeof buf, "%.*e", p - 1, v);
    if (std::strtod(buf, nullptr) == v) break;
  }
  // Exponente sin '+' ni ceros a la izquierda: 1e+20 -> 1e20, 1e-05 -> 1e-5 (el lexer los acepta).
  std::string s = buf;
  const size_t e = s.find('e');
  if (e != std::string::npos) {
    std::string mant = s.substr(0, e), sign, digits = s.substr(e + 1);
    if (!digits.empty() && (digits[0] == '+' || digits[0] == '-')) {
      if (digits[0] == '-') sign = "-";
      digits.erase(0, 1);
    }
    const size_t nz = digits.find_first_not_of('0');
    digits = nz == std::string::npos ? "0" : digits.substr(nz);
    s = mant + "e" + sign + digits;
  }
  return s;
}

std::string quote(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    switch (c) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      default: out += c;
    }
  }
  return out + "\"";
}

std::string typeStr(const TypeExpr& t) {
  switch (t.kind) {
    case TypeExpr::Kind::Name: return t.name;
    case TypeExpr::Kind::List: return "List[" + typeStr(*t.args[0]) + "]";
    case TypeExpr::Kind::Dict:
      return "Dict[" + typeStr(*t.args[0]) + ", " + typeStr(*t.args[1]) + "]";
    case TypeExpr::Kind::Tensor: {
      std::string s = "Tensor[" + typeStr(*t.args[0]) + ", (";
      for (size_t i = 0; i < t.dims.size(); ++i) {
        if (i) s += ", ";
        s += std::to_string(t.dims[i]);
      }
      return s + ")]";
    }
    case TypeExpr::Kind::Option: return "Option[" + typeStr(*t.args[0]) + "]";
    case TypeExpr::Kind::Hilo: return "Hilo[" + typeStr(*t.args[0]) + "]";
    case TypeExpr::Kind::Canal: return "Canal[" + typeStr(*t.args[0]) + "]";
    case TypeExpr::Kind::Debil: return "Debil[" + typeStr(*t.args[0]) + "]";
    case TypeExpr::Kind::Func: {
      std::string s = "fn(";
      for (size_t i = 0; i < t.args.size(); ++i) {
        if (i) s += ", ";
        s += typeStr(*t.args[i]);
      }
      s += ")";
      if (t.ret) s += " -> " + typeStr(*t.ret);
      return s;
    }
  }
  return "?";
}

std::string paramText(const Param& p) {
  if (p.isSelf) return p.selfMut ? "mut self" : "self";
  if (!p.annot) return p.name;
  return p.name + ": " + typeStr(*p.annot);
}

int leadingSpaces(const std::string& t) {
  const size_t p = t.find_first_not_of(' ');
  return static_cast<int>(p == std::string::npos ? t.size() : p);
}

std::string trimmed(const std::string& t) {
  const size_t b = t.find_first_not_of(" \t");
  if (b == std::string::npos) return "";
  const size_t e = t.find_last_not_of(" \t\r");
  return t.substr(b, e - b + 1);
}

// `t` empieza por la palabra `w` (no por un prefijo de un identificador más largo).
bool startsWithWord(const std::string& t, const std::string& w) {
  if (t.compare(0, w.size(), w) != 0) return false;
  if (t.size() == w.size()) return true;
  const char c = t[w.size()];
  return !(std::isalnum(static_cast<unsigned char>(c)) || c == '_');
}

class Printer {
 public:
  explicit Printer(const std::string& source)
      : lines_(splitLines(source)), comments_(collectComments(source)) {
    consumed_.assign(comments_.size(), false);
    for (size_t i = 0; i < comments_.size(); ++i) {
      if (comments_[i].trailing) trailingAt_.emplace(comments_[i].line, i);
    }
  }

  std::string program(const Program& prog) {
    struct Item {
      int line;
      const Decl* decl;
      const Stmt* stmt;
    };
    std::vector<Item> items;
    for (const auto& d : prog.decls) items.push_back({d->loc.line, d.get(), nullptr});
    for (const auto& s : prog.stmts) items.push_back({s->loc.line, nullptr, s.get()});
    std::stable_sort(items.begin(), items.end(),
                     [](const Item& a, const Item& b) { return a.line < b.line; });

    bool any = false;
    bool prevDecl = false;
    for (size_t i = 0; i < items.size(); ++i) {
      const Item& it = items[i];
      const bool isDecl = it.decl != nullptr;
      const int next = i + 1 < items.size() ? items[i + 1].line : kInf;
      any = emitBefore(it.line, 0, any, isDecl || prevDecl);
      if (isDecl) {
        decl(*it.decl, next);
      } else {
        stmt(*it.stmt, 0, next);
      }
      any = true;
      prevDecl = isDecl;
    }
    emitBefore(kInf, 0, any, prevDecl, /*gapItem=*/false);
    return out_;
  }

 private:
  static constexpr int kInf = INT_MAX;

  // ------------------------------------------------------------ salida y comentarios
  static std::string indentStr(int level) { return std::string(static_cast<size_t>(level) * 4, ' '); }

  // La línea anterior a `line` (1-based) está en blanco en el texto original.
  bool blankBefore(int line) const {
    if (line < 2 || static_cast<size_t>(line - 1) > lines_.size()) return false;
    for (char c : lines_[static_cast<size_t>(line) - 2]) {
      if (c != ' ' && c != '\t') return false;
    }
    return true;
  }

  // Línea en blanco, sin duplicarla ni ponerla al principio del archivo.
  void blank() {
    if (out_.size() < 2) return;                               // nada antes del primer elemento
    if (out_.compare(out_.size() - 2, 2, "\n\n") == 0) return;  // nunca dos en blanco seguidas
    out_ += '\n';
  }

  // Una línea de código, con el comentario de cola de `srcLine` si lo hay.
  void emitLine(int indent, const std::string& text, int srcLine) {
    out_ += indentStr(indent);
    out_ += text;
    auto it = trailingAt_.find(srcLine);
    if (it != trailingAt_.end() && !consumed_[it->second]) {
      out_ += "  ";
      out_ += comments_[it->second].text;
      consumed_[it->second] = true;
    }
    out_ += '\n';
  }

  // Emite los comentarios de línea completa anteriores a la línea `at`, con indentación `indent`.
  // Devuelve si ya hay algo emitido en el bloque. `forced`: el elemento va precedido de una línea
  // en blanco (declaraciones de nivel superior). `gapItem` false: no se pone línea en blanco antes
  // del propio elemento (else/elif).
  bool emitBefore(int at, int indent, bool any, bool forced, bool gapItem = true) {
    bool flushed = false;
    while (next_ < comments_.size() && comments_[next_].line < at) {
      const size_t i = next_++;
      if (consumed_[i]) continue;
      const Comment& c = comments_[i];
      if (any && ((forced && !flushed) || blankBefore(c.line))) blank();
      out_ += indentStr(indent) + c.text + "\n";
      consumed_[i] = true;
      any = true;
      flushed = true;
    }
    if (any && gapItem && ((forced && !flushed) || blankBefore(at))) blank();
    return any;
  }

  // Comentarios de línea completa que siguen al último elemento de un bloque. Pertenecen al bloque
  // si su columna es >= a la del bloque (`srcIndent` = espacios de sus sentencias) y están antes de
  // `bound`, la línea del siguiente elemento de la escala exterior.
  void tail(int srcIndent, int indent, int bound) {
    while (next_ < comments_.size()) {
      const size_t i = next_;
      if (consumed_[i]) {
        ++next_;
        continue;
      }
      const Comment& c = comments_[i];
      if (c.line >= bound || c.col - 1 < srcIndent) break;
      if (blankBefore(c.line)) blank();
      out_ += indentStr(indent) + c.text + "\n";
      consumed_[i] = true;
      ++next_;
    }
  }

  // ------------------------------------------------------------ expresiones
  // Imprime `e` y lo envuelve en paréntesis si su precedencia es menor que `minPrec`.
  std::string expr(const Expr& e, int minPrec) const {
    std::string s = exprText(e);
    return precOf(e) < minPrec ? "(" + s + ")" : s;
  }

  // Añadir aquí los casos de nuevas ExprKind.
  std::string exprText(const Expr& e) const {
    switch (e.kind) {
      case ExprKind::IntLit: return std::to_string(static_cast<const IntLit&>(e).value);
      case ExprKind::FloatLit: return formatFloat(static_cast<const FloatLit&>(e).value);
      case ExprKind::BoolLit: return static_cast<const BoolLit&>(e).value ? "true" : "false";
      case ExprKind::StrLit: return quote(static_cast<const StrLit&>(e).value);
      case ExprKind::NoneLit: return "None";
      case ExprKind::Ident: return static_cast<const Ident&>(e).name;
      case ExprKind::Unary: {
        const auto& u = static_cast<const Unary&>(e);
        if (u.op == UnaryOp::Neg) return "-" + expr(*u.operand, kNeg);
        return "not " + expr(*u.operand, kNot);
      }
      case ExprKind::Binary: {
        const auto& b = static_cast<const Binary&>(e);
        const BinInfo info = binInfo(b.op);
        // Asociatividad por la izquierda: la izquierda admite su misma precedencia, la derecha no.
        return expr(*b.lhs, info.prec) + " " + info.text + " " + expr(*b.rhs, info.prec + 1);
      }
      case ExprKind::Await: return "await " + expr(*static_cast<const AwaitExpr&>(e).operand, kNeg);
      case ExprKind::Call: {
        const auto& c = static_cast<const Call&>(e);
        if (!c.typeArgs.empty()) {  // Canal[T]()
          return expr(*c.callee, kPostfix) + "[" + typeStr(*c.typeArgs[0]) + "]()";
        }
        const std::string prefijo = c.spawn ? "hilo " : "";
        std::string s = prefijo + expr(*c.callee, kPostfix) + "(";
        for (size_t i = 0; i < c.args.size(); ++i) {
          if (i) s += ", ";
          if (i < c.argNames.size() && !c.argNames[i].empty()) s += c.argNames[i] + ": ";
          s += expr(*c.args[i], kLowest);
        }
        return s + ")";
      }
      case ExprKind::Index: {
        const auto& ix = static_cast<const Index&>(e);
        std::string s = expr(*ix.object, kPostfix) + "[" + expr(*ix.index, kLowest);
        for (const auto& r : ix.rest) s += ", " + expr(*r, kLowest);
        return s + "]";
      }
      case ExprKind::Member: {
        const auto& m = static_cast<const Member&>(e);
        return expr(*m.object, kPostfix) + "." + m.name;
      }
      case ExprKind::ListLit: {
        const auto& l = static_cast<const ListLit&>(e);
        std::string s = "[";
        for (size_t i = 0; i < l.elems.size(); ++i) {
          if (i) s += ", ";
          s += expr(*l.elems[i], kLowest);
        }
        return s + "]";
      }
      case ExprKind::DictLit: {
        const auto& d = static_cast<const DictLit&>(e);
        std::string s = "{";
        for (size_t i = 0; i < d.entries.size(); ++i) {
          if (i) s += ", ";
          s += expr(*d.entries[i].first, kLowest) + ": " + expr(*d.entries[i].second, kLowest);
        }
        return s + "}";
      }
      case ExprKind::Range: {
        const auto& r = static_cast<const RangeExpr&>(e);
        return expr(*r.start, kLowest) + ".." + expr(*r.end, kLowest);
      }
    }
    return "?";
  }

  // ------------------------------------------------------------ sentencias
  void block(const std::vector<StmtPtr>& body, int indent, int bound) {
    if (body.empty()) return;  // el parser no produce bloques vacíos
    bool any = false;
    for (size_t i = 0; i < body.size(); ++i) {
      const Stmt& s = *body[i];
      const int next = i + 1 < body.size() ? body[i + 1]->loc.line : bound;
      any = emitBefore(s.loc.line, indent, any, false);
      stmt(s, indent, next);
      any = true;
    }
    tail(body[0]->loc.col - 1, indent, bound);
  }

  // Añadir aquí los casos de nuevas StmtKind.
  void stmt(const Stmt& s, int indent, int bound) {
    switch (s.kind) {
      case StmtKind::VarDecl: {
        const auto& v = static_cast<const VarDecl&>(s);
        std::string t = std::string(v.isMut ? "mut " : "") + v.name;
        if (v.annot) t += ": " + typeStr(*v.annot);
        t += " = " + expr(*v.init, kLowest);
        emitLine(indent, t, s.loc.line);
        return;
      }
      case StmtKind::Assign: {
        const auto& a = static_cast<const Assign&>(s);
        emitLine(indent, expr(*a.target, kLowest) + " = " + expr(*a.value, kLowest), s.loc.line);
        return;
      }
      case StmtKind::ExprStmt: {
        const auto& e = static_cast<const ExprStmt&>(s);
        emitLine(indent, expr(*e.expr, kLowest), s.loc.line);
        return;
      }
      case StmtKind::Return: {
        const auto& r = static_cast<const ReturnStmt&>(s);
        emitLine(indent, r.value ? "return " + expr(*r.value, kLowest) : std::string("return"), s.loc.line);
        return;
      }
      case StmtKind::If: ifChain(static_cast<const IfStmt&>(s), "if", indent, bound); return;
      case StmtKind::For: {
        const auto& f = static_cast<const ForStmt&>(s);
        emitLine(indent, "for " + f.var + " in " + expr(*f.iter, kLowest) + ":", s.loc.line);
        block(f.body, indent + 1, bound);
        return;
      }
      case StmtKind::While: {
        const auto& w = static_cast<const WhileStmt&>(s);
        emitLine(indent, "while " + expr(*w.cond, kLowest) + ":", s.loc.line);
        block(w.body, indent + 1, bound);
        return;
      }
      case StmtKind::Break: emitLine(indent, "break", s.loc.line); return;
      case StmtKind::Continue: emitLine(indent, "continue", s.loc.line); return;
      case StmtKind::Fn: fnDef(*static_cast<const FnStmt&>(s).fn, indent, bound); return;
    }
  }

  // `if` / `elif`. Un else cuyo único elemento es un if que en el original empezaba por `elif`
  // se vuelve a imprimir como elif; un `else:` seguido de un if anidado se queda como else.
  void ifChain(const IfStmt& s, const char* kw, int indent, int bound) {
    const IfStmt* elif = nullptr;
    int elseLine = 0;
    if (!s.elseBody.empty()) {
      const Stmt& first = *s.elseBody[0];
      if (s.elseBody.size() == 1 && first.kind == StmtKind::If &&
          startsWithWord(trimmed(lineAt(first.loc.line)), "elif")) {
        elif = static_cast<const IfStmt*>(&first);
      } else {
        elseLine = findElseLine(first.loc.line, s.loc.col - 1);
      }
    }
    const int thenBound = elif ? elif->loc.line : (elseLine ? elseLine : bound);
    emitLine(indent, std::string(kw) + " " + expr(*s.cond, kLowest) + ":", s.loc.line);
    block(s.thenBody, indent + 1, thenBound);
    if (elif) {
      emitBefore(elif->loc.line, indent, true, false, false);
      ifChain(*elif, "elif", indent, bound);
    } else if (!s.elseBody.empty()) {
      emitBefore(elseLine, indent, true, false, false);
      emitLine(indent, "else:", elseLine);
      block(s.elseBody, indent + 1, bound);
    }
  }

  // Línea del `else:` que precede a la primera sentencia de su bloque. `ifCol` son los espacios
  // de la línea del if: el else va con la misma indentación.
  int findElseLine(int bodyLine, int ifCol) const {
    for (int l = bodyLine - 1; l >= 1; --l) {
      const std::string& t = lineAt(l);
      if (leadingSpaces(t) == ifCol && startsWithWord(trimmed(t), "else")) return l;
    }
    return bodyLine;
  }

  const std::string& lineAt(int line) const {
    static const std::string empty;
    if (line < 1 || static_cast<size_t>(line) > lines_.size()) return empty;
    return lines_[static_cast<size_t>(line) - 1];
  }

  // ------------------------------------------------------------ declaraciones
  // Añadir aquí los casos de nuevas DeclKind.
  void decl(const Decl& d, int bound) {
    switch (d.kind) {
      case DeclKind::Fn: fnDef(static_cast<const FnDecl&>(d), 0, bound); return;
      case DeclKind::Struct: {
        const auto& st = static_cast<const StructDecl&>(d);
        emitLine(0, "struct " + st.name + ":", d.loc.line);
        bool any = false;
        for (const FieldDecl& f : st.fields) {
          any = emitBefore(f.loc.line, 1, any, false);
          emitLine(1, f.name + ": " + typeStr(*f.annot), f.loc.line);
          any = true;
        }
        if (!st.fields.empty()) tail(st.fields[0].loc.col - 1, 1, bound);
        return;
      }
      case DeclKind::Impl: {
        const auto& im = static_cast<const ImplDecl&>(d);
        emitLine(0, "impl " + im.typeName + ":", d.loc.line);
        bool any = false;
        for (size_t i = 0; i < im.methods.size(); ++i) {
          const FnDecl& m = *im.methods[i];
          const int next = i + 1 < im.methods.size() ? im.methods[i + 1]->loc.line : bound;
          any = emitBefore(m.loc.line, 1, any, true);
          fnDef(m, 1, next);
          any = true;
        }
        if (!im.methods.empty()) tail(im.methods[0]->loc.col - 1, 1, bound);
        return;
      }
      case DeclKind::Import:
        emitLine(0, "import " + static_cast<const ImportDecl&>(d).path, d.loc.line);
        return;
    }
  }

  void fnDef(const FnDecl& f, int indent, int bound) {
    std::string head = f.isExtern ? "extern \"" + f.abi + "\" fn " : f.isAsync ? std::string("async fn ") : std::string("fn ");
    head += f.name + "(";
    for (size_t i = 0; i < f.params.size(); ++i) {
      if (i) head += ", ";
      head += paramText(f.params[i]);
    }
    head += ")";
    if (f.retAnnot) head += " -> " + typeStr(*f.retAnnot);
    if (!f.hasBody) {
      emitLine(indent, head, f.loc.line);
      return;
    }
    emitLine(indent, head + ":", f.loc.line);
    block(f.body, indent + 1, bound);
  }

  // ------------------------------------------------------------ estado
  std::vector<std::string> lines_;
  std::vector<Comment> comments_;
  std::vector<bool> consumed_;
  std::map<int, size_t> trailingAt_;  // línea -> índice del comentario de cola
  size_t next_ = 0;                   // primer comentario aún no tratado
  std::string out_;
};

}  // namespace

std::string formatProgram(const Program& prog, const std::string& source) {
  Printer printer(source);
  return printer.program(prog);
}

}  // namespace lgfmt
