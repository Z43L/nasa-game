// Parser descendente recursivo. Recuperación de errores: si falla una sentencia se
// resincroniza en el NEWLINE de su mismo nivel de indentación, así se reportan varios
// errores en una sola pasada.
#include "parser/parser.h"

#include <utility>
#include <vector>

#include "lexer/lexer.h"

namespace lg {
namespace {

// Señal interna: el error ya está registrado en Diagnostics; quien la captura resincroniza.
struct ParseError {};

// Límite de anidación (expresiones, tipos y bloques) para no agotar la pila de C++.
constexpr int kMaxNesting = 1000;

// Incrementa un contador de anidación mientras vive el objeto (se restaura al salir, también por excepción).
class DepthScope {
 public:
  explicit DepthScope(int& counter) : counter_(counter) { ++counter_; }
  ~DepthScope() { --counter_; }
  DepthScope(const DepthScope&) = delete;
  DepthScope& operator=(const DepthScope&) = delete;

 private:
  int& counter_;
};

constexpr const char* kTypeHint =
    "tipos válidos: int, float, bool, str, List[T], Dict[K, V], Option[T], Tensor[T, (d1, d2)], Hilo[T], Canal[T], Debil[T], fn(A) -> R";

bool isDeclKeyword(TokenKind k) {
  return k == TokenKind::KwFn || k == TokenKind::KwStruct || k == TokenKind::KwImpl ||
         k == TokenKind::KwExtern || k == TokenKind::KwImport || k == TokenKind::KwAsync;
}

bool isBlockKeyword(TokenKind k) {
  switch (k) {
    case TokenKind::KwFn:
    case TokenKind::KwIf:
    case TokenKind::KwElif:
    case TokenKind::KwElse:
    case TokenKind::KwFor:
    case TokenKind::KwWhile:
    case TokenKind::KwStruct:
    case TokenKind::KwImpl:
      return true;
    default:
      return false;
  }
}

std::string describe(const Token& t) {
  switch (t.kind) {
    case TokenKind::Ident: return "el identificador '" + t.text + "'";
    case TokenKind::Int:
    case TokenKind::Float: return "el número '" + t.text + "'";
    case TokenKind::String: return "una cadena";
    default: return tokenKindName(t.kind);
  }
}

class Parser {
 public:
  Parser(std::string file, std::vector<Token> tokens, Diagnostics& diags)
      : file_(std::move(file)), toks_(std::move(tokens)), diags_(diags) {}

  std::unique_ptr<Program> parseModule() {
    auto prog = std::make_unique<Program>();
    prog->file = file_;
    while (!check(TokenKind::End)) {
      if (check(TokenKind::Dedent)) {  // solo aparece tras un INDENT inesperado ya reportado
        diags_.error(here(), "indentación inconsistente", "revisa la indentación de la línea anterior");
        advance();
        continue;
      }
      const size_t start = pos_;
      try {
        if (isDeclKeyword(peek().kind)) {
          prog->decls.push_back(parseDecl());
        } else {
          prog->stmts.push_back(parseStatement());
        }
      } catch (const ParseError&) {
        recover(start);
      }
    }
    return prog;
  }

 private:
  // ---------------------------------------------------------------- tokens
  const Token& peek(size_t k = 0) const {
    const size_t i = pos_ + k;
    return toks_[i < toks_.size() ? i : toks_.size() - 1];
  }
  bool check(TokenKind k) const { return peek().kind == k; }
  const Token& advance() {
    const Token& t = toks_[pos_];
    if (pos_ + 1 < toks_.size()) ++pos_;
    return t;
  }
  bool match(TokenKind k) {
    if (check(k)) {
      advance();
      return true;
    }
    return false;
  }
  Loc locOf(const Token& t) const { return Loc{file_, t.line, t.col}; }
  Loc here() const { return locOf(peek()); }

  // ---------------------------------------------------------------- errores
  [[noreturn]] void fail(const Loc& loc, const std::string& msg, const std::string& hint = "") {
    diags_.error(loc, msg, hint);
    throw ParseError{};
  }

  [[noreturn]] void failExpected(const std::string& what, const std::string& hint = "") {
    if (check(TokenKind::DotDot)) {
      fail(here(), "'..' solo puede usarse en la cabecera de un for",
           "escribe, por ejemplo: for i in 0..10:");
    }
    fail(here(), "se esperaba " + what + ", pero se encontró " + describe(peek()), hint);
  }

  const Token& expect(TokenKind k, const std::string& what, const std::string& hint = "") {
    if (!check(k)) failExpected(what, hint);
    return advance();
  }

  // Los dos puntos que faltan no son fatales: se reporta y se sigue, para no encadenar errores.
  void expectColon(const std::string& ctx) {
    if (match(TokenKind::Colon)) return;
    diags_.error(here(), "se esperaba ':' " + ctx, "añade ':' al final de la línea");
  }

  void endStatement() {
    if (match(TokenKind::Newline) || check(TokenKind::End)) return;
    failExpected("fin de línea", "termina cada sentencia con un salto de línea; el lenguaje no usa punto y coma");
  }

  // ---------------------------------------------------------------- recuperación
  // Avanza hasta el fin de la sentencia fallida. Se detiene tras NEWLINE de nivel 0 o
  // antes de un DEDENT que cierra el bloque padre. Salta los bloques anidados enteros.
  void synchronize() {
    int depth = 0;
    while (!check(TokenKind::End)) {
      const TokenKind k = peek().kind;
      if (k == TokenKind::Indent) {
        ++depth;
        advance();
      } else if (k == TokenKind::Dedent) {
        if (depth == 0) return;
        --depth;
        advance();
        if (depth == 0) return;
      } else if (k == TokenKind::Newline) {
        advance();
        if (depth == 0) return;
      } else {
        advance();
      }
    }
  }

  void skipIndentedBlock() {
    int depth = 0;
    while (!check(TokenKind::End)) {
      if (check(TokenKind::Indent)) {
        ++depth;
      } else if (check(TokenKind::Dedent)) {
        --depth;
        if (depth == 0) {
          advance();
          return;
        }
      }
      advance();
    }
  }

  // Tras un error en una sentencia que abre bloque, descarta también su bloque entero
  // para no reportar después una falsa "indentación inesperada".
  void recover(size_t start) {
    const TokenKind first = toks_[start].kind;
    synchronize();
    if (isBlockKeyword(first) && check(TokenKind::Indent)) skipIndentedBlock();
    // Tras un error de anidación, el if no se completa: se descarta también su cadena elif/else.
    if (nestingError_ && first == TokenKind::KwIf) skipRestOfIfChain();
    nestingError_ = false;
  }

  // Descarta los elif/else que siguen a un if cuyo parseo se abandonó (cabecera y bloque).
  void skipRestOfIfChain() {
    while (check(TokenKind::KwElif) || check(TokenKind::KwElse)) {
      while (!check(TokenKind::Newline) && !check(TokenKind::End)) advance();
      match(TokenKind::Newline);
      if (check(TokenKind::Indent)) skipIndentedBlock();
    }
  }

  // Bloque indentado: ya se consumió el ':' de la cabecera. Cada elemento se parsea con
  // recuperación de errores propia.
  template <typename Item>
  void indentedBlock(Item item) {
    if (!check(TokenKind::Newline)) {
      fail(here(), "se esperaba un bloque indentado después de ':'",
           "escribe las sentencias en las líneas siguientes, con más indentación");
    }
    if (blockDepth_ >= kMaxNesting) {
      // Se reporta una vez y se salta el bloque entero: no se lanza excepción, así no hay cascada.
      diags_.error(here(), "bloque demasiado anidado (límite 1000 niveles)",
                   "reduce la anidación: saca parte del código a una función");
      advance();
      if (check(TokenKind::Indent)) skipIndentedBlock();
      return;
    }
    advance();
    if (!check(TokenKind::Indent)) {
      fail(here(), "se esperaba un bloque indentado después de ':'",
           "escribe las sentencias en las líneas siguientes, con más indentación");
    }
    advance();
    DepthScope blockScope(blockDepth_);
    while (!check(TokenKind::Dedent) && !check(TokenKind::End)) {
      const size_t start = pos_;
      try {
        item();
      } catch (const ParseError&) {
        recover(start);
      }
    }
    match(TokenKind::Dedent);
  }

  std::vector<StmtPtr> parseBlock() {
    std::vector<StmtPtr> body;
    indentedBlock([&] { body.push_back(parseStatement()); });
    return body;
  }

  // ---------------------------------------------------------------- declaraciones
  std::unique_ptr<Decl> parseDecl() {
    switch (peek().kind) {
      case TokenKind::KwFn: return parseFn(false);
      case TokenKind::KwAsync: {
        // async fn: igual que fn, pero la llamada devuelve Hilo[T] (ver docs/referencia/hilos.md).
        advance();
        if (!check(TokenKind::KwFn)) failExpected("'fn' después de 'async'", "escribe: async fn nombre(a: int) -> int:");
        auto fn = parseFn(false);
        fn->isAsync = true;
        return fn;
      }
      case TokenKind::KwStruct: return parseStruct();
      case TokenKind::KwImpl: return parseImpl();
      case TokenKind::KwExtern: return parseExtern();
      case TokenKind::KwImport: return parseImport();
      default: break;
    }
    failExpected("una declaración");
  }

  std::unique_ptr<FnDecl> parseFn(bool inImpl) {
    auto fn = std::make_unique<FnDecl>(locOf(advance()));
    fn->name = expect(TokenKind::Ident, "el nombre de la función", "escribe: fn nombre(a: int) -> int:").text;
    parseParams(*fn, inImpl);
    if (match(TokenKind::Arrow)) fn->retAnnot = parseType();
    expectColon("después de la firma de la función");
    fn->body = parseBlock();
    return fn;
  }

  std::unique_ptr<FnDecl> parseExtern() {
    const Loc l = locOf(advance());
    if (!check(TokenKind::String)) {
      failExpected("la ABI entre comillas", "escribe: extern \"C\" fn nombre(...) -> tipo");
    }
    const std::string abi = advance().text;
    if (abi != "C") fail(l, "ABI no soportada '" + abi + "'", "usa extern \"C\"");
    expect(TokenKind::KwFn, "'fn' después de la ABI", "escribe: extern \"C\" fn nombre(...)");
    auto fn = std::make_unique<FnDecl>(l);
    fn->isExtern = true;
    fn->hasBody = false;
    fn->abi = abi;
    fn->name = expect(TokenKind::Ident, "el nombre de la función", "escribe: extern \"C\" fn nombre(...)").text;
    parseParams(*fn, false);
    if (match(TokenKind::Arrow)) fn->retAnnot = parseType();
    if (check(TokenKind::Colon)) {
      diags_.error(here(), "las funciones extern no tienen cuerpo",
                   "quita los dos puntos: la firma de una función extern termina en su línea");
      advance();
      parseBlock();  // se descarta el cuerpo erróneo
    } else {
      endStatement();
    }
    return fn;
  }

  std::unique_ptr<StructDecl> parseStruct() {
    auto st = std::make_unique<StructDecl>(locOf(advance()));
    st->name = expect(TokenKind::Ident, "el nombre de la struct", "escribe: struct Punto:").text;
    expectColon("después del nombre de la struct");
    indentedBlock([&] {
      const Token& nameTok = expect(TokenKind::Ident, "el nombre de un campo", "escribe los campos como nombre: tipo");
      FieldDecl field;
      field.name = nameTok.text;
      field.loc = locOf(nameTok);
      expect(TokenKind::Colon, "':' después del nombre del campo", "escribe el campo como " + field.name + ": float");
      field.annot = parseType();
      endStatement();
      st->fields.push_back(std::move(field));
    });
    return st;
  }

  std::unique_ptr<ImplDecl> parseImpl() {
    auto impl = std::make_unique<ImplDecl>(locOf(advance()));
    const Token& typeTok = expect(TokenKind::Ident, "el nombre del tipo", "escribe: impl Punto:");
    impl->typeName = typeTok.text;
    impl->typeLoc = locOf(typeTok);
    expectColon("después del nombre del tipo en 'impl'");
    indentedBlock([&] {
      if (!check(TokenKind::KwFn)) {
        fail(here(), "en un bloque 'impl' solo se permiten funciones", "declara los métodos con 'fn'");
      }
      impl->methods.push_back(parseFn(true));
    });
    return impl;
  }

  std::unique_ptr<ImportDecl> parseImport() {
    auto imp = std::make_unique<ImportDecl>(locOf(advance()));
    std::string path = expect(TokenKind::Ident, "el nombre del módulo después de 'import'", "escribe: import util").text;
    while (match(TokenKind::Slash)) {
      path += "/" + expect(TokenKind::Ident, "un nombre de carpeta o módulo después de '/'").text;
    }
    imp->path = path;
    endStatement();
    return imp;
  }

  void parseParams(FnDecl& fn, bool inImpl) {
    expect(TokenKind::LParen, "'(' después del nombre de la función", "escribe: fn nombre(a: int) -> int:");
    while (!check(TokenKind::RParen)) {
      fn.params.push_back(parseParam(fn.params.size(), inImpl));
      if (!match(TokenKind::Comma)) break;
    }
    expect(TokenKind::RParen, "')' al final de los parámetros");
  }

  Param parseParam(size_t index, bool inImpl) {
    Param p;
    p.loc = here();
    const bool mutTok = match(TokenKind::KwMut);
    if (check(TokenKind::Ident) && peek().text == "self" && peek(1).kind != TokenKind::Colon) {
      if (!inImpl) {
        fail(p.loc, "'self' solo es válido en métodos", "declara la función dentro de un bloque 'impl Tipo:'");
      }
      if (index != 0) fail(p.loc, "'self' debe ser el primer parámetro", "escribe: fn metodo(self, ...)");
      advance();
      p.name = "self";
      p.isSelf = true;
      p.selfMut = mutTok;
      return p;
    }
    if (mutTok) {
      fail(p.loc, "'mut' solo puede usarse como 'mut self' en la firma de un método",
           "los parámetros normales se escriben nombre: tipo");
    }
    const Token& nameTok = expect(TokenKind::Ident, "el nombre del parámetro", "escribe los parámetros como nombre: tipo");
    p.name = nameTok.text;
    p.loc = locOf(nameTok);
    if (!match(TokenKind::Colon)) {
      fail(here(), "se esperaba ':' y el tipo del parámetro '" + p.name + "'",
           "escribe el parámetro como " + p.name + ": int");
    }
    p.annot = parseType();
    return p;
  }

  // ---------------------------------------------------------------- tipos
  TypeExprPtr parseType() {
    checkNesting(exprDepth_, "tipo demasiado anidado (límite 1000 niveles)", "simplifica el tipo");
    DepthScope scope(exprDepth_);
    const Token& t = peek();
    if (t.kind == TokenKind::KwFn) {
      // Tipo de función: fn(A, B) -> R (sin "-> R" el retorno es void).
      auto te = std::make_unique<TypeExpr>();
      te->loc = locOf(advance());
      te->kind = TypeExpr::Kind::Func;
      expect(TokenKind::LParen, "'(' con los parámetros del tipo función", "escribe, por ejemplo, fn(int) -> int");
      while (!check(TokenKind::RParen)) {
        te->args.push_back(parseType());
        if (!match(TokenKind::Comma)) break;
      }
      expect(TokenKind::RParen, "')' al final de los parámetros del tipo función", "escribe, por ejemplo, fn(int) -> int");
      if (match(TokenKind::Arrow)) te->ret = parseType();
      return te;
    }
    if (t.kind != TokenKind::Ident) failExpected("un tipo", kTypeHint);
    const std::string name = t.text;
    auto te = std::make_unique<TypeExpr>();
    te->loc = locOf(t);
    if (name != "List" && name != "Dict" && name != "Tensor" && name != "Option" && name != "Hilo" &&
        name != "Canal" && name != "Debil") {
      advance();
      te->kind = TypeExpr::Kind::Name;
      te->name = name;
      // alias.Tipo: tipo de un módulo importado (el cargador lo resuelve).
      if (check(TokenKind::Dot) && peek(1).kind == TokenKind::Ident) {
        advance();
        te->name += "." + advance().text;
      }
      return te;
    }
    advance();
    expect(TokenKind::LBracket, "'[' después de '" + name + "'", kTypeHint);
    if (name == "List") {
      te->kind = TypeExpr::Kind::List;
      te->args.push_back(parseType());
    } else if (name == "Option") {
      te->kind = TypeExpr::Kind::Option;
      te->args.push_back(parseType());
    } else if (name == "Hilo" || name == "Canal" || name == "Debil") {
      te->kind = name == "Hilo" ? TypeExpr::Kind::Hilo : name == "Canal" ? TypeExpr::Kind::Canal : TypeExpr::Kind::Debil;
      te->args.push_back(parseType());
    } else if (name == "Dict") {
      te->kind = TypeExpr::Kind::Dict;
      te->args.push_back(parseType());
      expect(TokenKind::Comma, "',' entre la clave y el valor de Dict", "escribe Dict[str, int]");
      te->args.push_back(parseType());
    } else {
      te->kind = TypeExpr::Kind::Tensor;
      TypeExprPtr elem = parseType();
      if (elem->kind != TypeExpr::Kind::Name || (elem->name != "int" && elem->name != "float")) {
        fail(elem->loc, "Tensor solo admite elementos int o float", "escribe, por ejemplo, Tensor[float, (2, 3)]");
      }
      te->args.push_back(std::move(elem));
      expect(TokenKind::Comma, "',' antes de las dimensiones del Tensor", "escribe Tensor[float, (2, 3)]");
      expect(TokenKind::LParen, "'(' con las dimensiones del Tensor", "escribe las dimensiones entre paréntesis: (2, 3)");
      while (!check(TokenKind::RParen)) {
        const Token& d = expect(TokenKind::Int, "una dimensión entera", "las dimensiones son enteros positivos, p. ej. (2, 3)");
        if (d.intValue <= 0) fail(locOf(d), "la dimensión debe ser mayor que 0");
        te->dims.push_back(d.intValue);
        if (!match(TokenKind::Comma)) break;
      }
      if (te->dims.empty()) {
        fail(here(), "el Tensor necesita al menos una dimensión", "escribe, por ejemplo, Tensor[float, (128)]");
      }
      expect(TokenKind::RParen, "')' para cerrar las dimensiones del Tensor", "escribe las dimensiones entre paréntesis: (2, 3)");
    }
    expect(TokenKind::RBracket, "']' para cerrar el tipo", kTypeHint);
    return te;
  }

  // ---------------------------------------------------------------- sentencias
  StmtPtr parseStatement() {
    const Token& t = peek();
    const Loc l = locOf(t);
    switch (t.kind) {
      case TokenKind::KwIf:
        advance();
        return parseIf(l, "if");
      case TokenKind::KwFor:
        advance();
        return parseFor(l);
      case TokenKind::KwWhile: {
        advance();
        auto node = std::make_unique<WhileStmt>(l);
        node->cond = parseExpr();
        expectColon("después de la condición del while");
        node->body = parseBlock();
        return node;
      }
      case TokenKind::KwBreak:
        advance();
        endStatement();
        return std::make_unique<BreakStmt>(l);
      case TokenKind::KwContinue:
        advance();
        endStatement();
        return std::make_unique<ContinueStmt>(l);
      case TokenKind::KwReturn: {
        advance();
        auto node = std::make_unique<ReturnStmt>(l);
        if (!check(TokenKind::Newline) && !check(TokenKind::End)) node->value = parseExpr();
        endStatement();
        return node;
      }
      case TokenKind::KwMut:
        return parseDeclStmt(l);
      case TokenKind::KwFn: {
        // Función anidada (clausura): solo visible en el bloque donde se declara.
        auto node = std::make_unique<FnStmt>(l);
        node->fn = parseFn(false);
        return node;
      }
      case TokenKind::KwElif:
      case TokenKind::KwElse:
        fail(l, std::string(tokenKindName(t.kind)) + " sin 'if' correspondiente",
             "el else/elif debe ir justo después del bloque del if, con la misma indentación");
      case TokenKind::Indent:
        fail(l, "indentación inesperada",
             "esta línea está más indentada que la anterior; quita espacios o termina la línea anterior en ':'");
      default:
        break;
    }
    if (isDeclKeyword(t.kind)) {
      fail(l, std::string(tokenKindName(t.kind)) + " solo puede declararse a nivel superior",
           "saca la declaración al nivel principal, sin indentación");
    }
    if (t.kind == TokenKind::Ident && peek(1).kind == TokenKind::Colon) return parseDeclStmt(l);

    ExprPtr e = parseExpr();
    if (match(TokenKind::Assign)) {
      if (e->kind != ExprKind::Ident && e->kind != ExprKind::Index && e->kind != ExprKind::Member) {
        fail(e->loc, "destino de asignación no válido",
             "solo se asigna a variables, a índices (lista[i]) o a campos (obj.campo)");
      }
      auto node = std::make_unique<Assign>(l);
      node->target = std::move(e);
      node->value = parseExpr();
      endStatement();
      return node;
    }
    auto node = std::make_unique<ExprStmt>(l);
    node->expr = std::move(e);
    endStatement();
    return node;
  }

  // `mut NAME [: T] = e`  o  `NAME: T = e`
  StmtPtr parseDeclStmt(const Loc& l) {
    auto decl = std::make_unique<VarDecl>(l);
    decl->isMut = match(TokenKind::KwMut);
    decl->name = expect(TokenKind::Ident, "un nombre de variable", "escribe: mut x = 0  o  x: int = 0").text;
    if (match(TokenKind::Colon)) decl->annot = parseType();
    if (!check(TokenKind::Assign)) {
      fail(here(), "se esperaba '=' en la declaración de '" + decl->name + "'",
           "las variables con 'mut' o con tipo necesitan valor inicial: x: int = 0");
    }
    advance();
    decl->init = parseExpr();
    endStatement();
    return decl;
  }

  // `if` / `elif` ya consumido. Los elif se desugaran a else: [IfStmt].
  std::unique_ptr<IfStmt> parseIf(const Loc& l, const char* kw) {
    auto node = std::make_unique<IfStmt>(l);
    node->cond = parseExpr();
    expectColon(std::string("después de la condición del ") + kw);
    node->thenBody = parseBlock();
    if (check(TokenKind::KwElif)) {
      // Cadena elif demasiado profunda: el cuerpo de este mismo elif ya reportó el error de
      // bloque anidado (misma profundidad), así que aquí solo se descarta el resto de la cadena.
      if (blockDepth_ >= kMaxNesting) {
        skipRestOfIfChain();
        return node;
      }
      DepthScope scope(blockDepth_);
      const Loc el = locOf(advance());
      node->elseBody.push_back(parseIf(el, "elif"));
    } else if (match(TokenKind::KwElse)) {
      expectColon("después de 'else'");
      node->elseBody = parseBlock();
    }
    return node;
  }

  StmtPtr parseFor(const Loc& l) {
    auto node = std::make_unique<ForStmt>(l);
    node->var = expect(TokenKind::Ident, "el nombre de la variable del for", "escribe: for i in 0..10:").text;
    expect(TokenKind::KwIn, "'in' después de la variable del for", "escribe: for i in 0..10:");
    ExprPtr first = parseExpr();
    if (match(TokenKind::DotDot)) {
      ExprPtr last = parseExpr();
      const Loc rl = first->loc;
      node->iter = std::make_unique<RangeExpr>(rl, std::move(first), std::move(last));
    } else {
      node->iter = std::move(first);
    }
    expectColon("después de la cabecera del for");
    node->body = parseBlock();
    return node;
  }

  // ---------------------------------------------------------------- expresiones
  // Precedencia de menor a mayor: or < and < not < comparaciones < +- < */%@ < unario- < postfijo
  // Lanza un error de anidación si la profundidad supera el límite (el error queda en el lugar actual).
  // Con `>` se admiten 1000 corchetes anidados con su elemento final: la hoja también abre un
  // nivel de parseExpr, así que el límite efectivo de contenedores es 1000.
  void checkNesting(int depth, const std::string& msg, const std::string& hint) {
    if (depth > kMaxNesting) {
      nestingError_ = true;
      fail(here(), msg, hint);
    }
  }

  ExprPtr parseExpr() {
    checkNesting(exprDepth_, "expresión demasiado anidada (límite 1000 niveles)",
                 "divide la expresión en variables intermedias");
    DepthScope scope(exprDepth_);
    return parseOr();
  }

  ExprPtr parseOr() {
    ExprPtr lhs = parseAnd();
    while (check(TokenKind::KwOr)) {
      const Loc l = locOf(advance());
      ExprPtr rhs = parseAnd();
      lhs = std::make_unique<Binary>(l, BinaryOp::Or, std::move(lhs), std::move(rhs));
    }
    return lhs;
  }

  ExprPtr parseAnd() {
    ExprPtr lhs = parseNot();
    while (check(TokenKind::KwAnd)) {
      const Loc l = locOf(advance());
      ExprPtr rhs = parseNot();
      lhs = std::make_unique<Binary>(l, BinaryOp::And, std::move(lhs), std::move(rhs));
    }
    return lhs;
  }

  ExprPtr parseNot() {
    if (check(TokenKind::KwNot)) {
      checkNesting(exprDepth_, "expresión demasiado anidada (límite 1000 niveles)",
                   "divide la expresión en variables intermedias");
      DepthScope scope(exprDepth_);
      const Loc l = locOf(advance());
      ExprPtr operand = parseNot();
      return std::make_unique<Unary>(l, UnaryOp::Not, std::move(operand));
    }
    return parseComparison();
  }

  ExprPtr parseComparison() {
    ExprPtr lhs = parseAdditive();
    for (;;) {
      BinaryOp op;
      switch (peek().kind) {
        case TokenKind::EqEq: op = BinaryOp::Eq; break;
        case TokenKind::NotEq: op = BinaryOp::Ne; break;
        case TokenKind::Lt: op = BinaryOp::Lt; break;
        case TokenKind::Gt: op = BinaryOp::Gt; break;
        case TokenKind::Le: op = BinaryOp::Le; break;
        case TokenKind::Ge: op = BinaryOp::Ge; break;
        case TokenKind::KwIn: op = BinaryOp::In; break;
        default: return lhs;
      }
      const Loc l = locOf(advance());
      ExprPtr rhs = parseAdditive();
      lhs = std::make_unique<Binary>(l, op, std::move(lhs), std::move(rhs));
    }
  }

  ExprPtr parseAdditive() {
    ExprPtr lhs = parseTerm();
    while (check(TokenKind::Plus) || check(TokenKind::Minus)) {
      const BinaryOp op = check(TokenKind::Plus) ? BinaryOp::Add : BinaryOp::Sub;
      const Loc l = locOf(advance());
      ExprPtr rhs = parseTerm();
      lhs = std::make_unique<Binary>(l, op, std::move(lhs), std::move(rhs));
    }
    return lhs;
  }

  ExprPtr parseTerm() {
    ExprPtr lhs = parseUnary();
    for (;;) {
      BinaryOp op;
      switch (peek().kind) {
        case TokenKind::Star: op = BinaryOp::Mul; break;
        case TokenKind::Slash: op = BinaryOp::Div; break;
        case TokenKind::Percent: op = BinaryOp::Mod; break;
        case TokenKind::At: op = BinaryOp::MatMul; break;
        default: return lhs;
      }
      const Loc l = locOf(advance());
      ExprPtr rhs = parseUnary();
      lhs = std::make_unique<Binary>(l, op, std::move(lhs), std::move(rhs));
    }
  }

  ExprPtr parseUnary() {
    if (check(TokenKind::KwHilo)) {
      // hilo f(args): la llamada se ejecuta en un hilo nuevo. Sema comprueba que sea f(...) de nivel superior.
      const Loc l = locOf(advance());
      ExprPtr e = parsePostfix();
      if (e->kind != ExprKind::Call) {
        fail(l, "'hilo' solo admite una llamada a función", "escribe: hilo f(args)");
      }
      static_cast<Call&>(*e).spawn = true;
      return e;
    }
    if (check(TokenKind::KwAwait)) {
      checkNesting(exprDepth_, "expresión demasiado anidada (límite 1000 niveles)",
                   "divide la expresión en variables intermedias");
      DepthScope scope(exprDepth_);
      const Loc l = locOf(advance());
      ExprPtr operand = parseUnary();
      return std::make_unique<AwaitExpr>(l, std::move(operand));
    }
    if (check(TokenKind::Minus)) {
      checkNesting(exprDepth_, "expresión demasiado anidada (límite 1000 niveles)",
                   "divide la expresión en variables intermedias");
      DepthScope scope(exprDepth_);
      const Loc l = locOf(advance());
      ExprPtr operand = parseUnary();
      return std::make_unique<Unary>(l, UnaryOp::Neg, std::move(operand));
    }
    return parsePostfix();
  }

  ExprPtr parsePostfix() {
    ExprPtr e = parsePrimary();
    for (;;) {
      if (check(TokenKind::LParen)) {
        const Loc l = e->loc;
        advance();
        auto call = std::make_unique<Call>(l, std::move(e));
        parseArgs(*call);
        expect(TokenKind::RParen, "')' para cerrar la llamada", "revisa que cada '(' tenga su ')'");
        e = std::move(call);
      } else if (check(TokenKind::LBracket)) {
        const Loc l = e->loc;
        advance();
        ExprPtr index = parseExpr();
        auto ix = std::make_unique<Index>(l, std::move(e), std::move(index));
        while (match(TokenKind::Comma)) ix->rest.push_back(parseExpr());
        expect(TokenKind::RBracket, "']' para cerrar el índice", "revisa que cada '[' tenga su ']'");
        e = std::move(ix);
      } else if (check(TokenKind::Dot)) {
        const Loc l = e->loc;
        advance();
        const std::string name = expect(TokenKind::Ident, "el nombre del campo o método después de '.'").text;
        e = std::make_unique<Member>(l, std::move(e), name);
      } else {
        return e;
      }
    }
  }

  void parseArgs(Call& call) {
    bool sawNamed = false;
    while (!check(TokenKind::RParen)) {
      std::string name;
      if (check(TokenKind::Ident) && peek(1).kind == TokenKind::Colon) {
        name = advance().text;
        advance();  // ':'
        sawNamed = true;
      } else if (check(TokenKind::Ident) && peek(1).kind == TokenKind::Assign) {
        // f(a=1): se acepta como argumento con nombre tras reportar el error, sin cascada.
        diags_.error(locOf(peek(1)), "para argumentos con nombre usa ':' (nombre: valor), no '='",
                     "escribe el argumento como " + peek().text + ": valor, p. ej. " + peek().text + ": 1");
        name = advance().text;
        advance();  // '='
        sawNamed = true;
      } else if (sawNamed) {
        fail(here(), "argumento posicional después de un argumento con nombre",
             "pon primero los argumentos posicionales");
      }
      call.args.push_back(parseExpr());
      call.argNames.push_back(std::move(name));
      if (!match(TokenKind::Comma)) break;
    }
  }

  ExprPtr parsePrimary() {
    const Token& t = peek();
    const Loc l = locOf(t);
    switch (t.kind) {
      case TokenKind::Int:
        advance();
        return std::make_unique<IntLit>(l, t.intValue);
      case TokenKind::Float:
        advance();
        return std::make_unique<FloatLit>(l, t.floatValue);
      case TokenKind::String:
        advance();
        return std::make_unique<StrLit>(l, t.text);
      case TokenKind::KwTrue:
        advance();
        return std::make_unique<BoolLit>(l, true);
      case TokenKind::KwFalse:
        advance();
        return std::make_unique<BoolLit>(l, false);
      case TokenKind::Ident:
        advance();
        if (t.text == "None") return std::make_unique<NoneLit>(l);
        if (t.text == "Canal" && check(TokenKind::LBracket)) {
          // Canal[T]() : constructor de canal con el tipo de los mensajes.
          advance();
          auto call = std::make_unique<Call>(l, std::make_unique<Ident>(l, "Canal"));
          call->typeArgs.push_back(parseType());
          expect(TokenKind::RBracket, "']' para cerrar el tipo del canal", "escribe: Canal[int]()");
          expect(TokenKind::LParen, "'(' después de Canal[T]", "escribe: Canal[int]()");
          expect(TokenKind::RParen, "')' para cerrar Canal[T]()", "escribe: Canal[int]()");
          return call;
        }
        return std::make_unique<Ident>(l, t.text);
      case TokenKind::LParen: {
        advance();
        ExprPtr inner = parseExpr();
        expect(TokenKind::RParen, "')' para cerrar el paréntesis", "revisa que cada '(' tenga su ')'");
        return inner;
      }
      case TokenKind::LBracket:
        return parseListLit();
      case TokenKind::LBrace:
        return parseDictLit();
      default:
        break;
    }
    failExpected("una expresión");
  }

  ExprPtr parseListLit() {
    auto list = std::make_unique<ListLit>(locOf(advance()));
    while (!check(TokenKind::RBracket)) {
      list->elems.push_back(parseExpr());
      if (!match(TokenKind::Comma)) break;
    }
    expect(TokenKind::RBracket, "']' para cerrar la lista", "revisa que cada '[' tenga su ']'");
    return list;
  }

  ExprPtr parseDictLit() {
    auto dict = std::make_unique<DictLit>(locOf(advance()));
    while (!check(TokenKind::RBrace)) {
      ExprPtr key = parseExpr();
      expect(TokenKind::Colon, "':' entre la clave y el valor", "escribe los diccionarios como {\"clave\": valor}");
      ExprPtr val = parseExpr();
      dict->entries.emplace_back(std::move(key), std::move(val));
      if (!match(TokenKind::Comma)) break;
    }
    expect(TokenKind::RBrace, "'}' para cerrar el diccionario", "revisa que cada '{' tenga su '}'");
    return dict;
  }

  // ---------------------------------------------------------------- estado
  std::string file_;
  std::vector<Token> toks_;
  Diagnostics& diags_;
  size_t pos_ = 0;
  int exprDepth_ = 0;         // niveles de expresión/tipo/unario anidados en curso
  int blockDepth_ = 0;        // niveles de bloque (y de elif) anidados en curso
  bool nestingError_ = false; // el último error fue de anidación (ver recover)
};

}  // namespace

std::unique_ptr<Program> parseProgram(const std::string& file, const std::string& source, Diagnostics& diags) {
  diags.registerSource(file, source);
  Parser parser(file, tokenize(file, source, diags), diags);
  return parser.parseModule();
}

}  // namespace lg
