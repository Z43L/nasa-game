// Pruebas unitarias de la sintaxis de concurrencia (hilo, async/await, Canal, Debil): parser y sema.
#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "ast/ast.h"
#include "common/diagnostics.h"
#include "parser/parser.h"
#include "sema/sema.h"
#include "sema/types.h"

using namespace lg;

namespace {

std::string errores(const Diagnostics& d) {
  std::ostringstream os;
  d.print(os);
  return os.str();
}

// Parsea y analiza `src`. Devuelve true si no hay errores de ninguna de las dos etapas.
bool analiza(const std::string& src, Diagnostics& d) {
  auto prog = parseProgram("t.lg", src, d);
  if (d.hasErrors()) return false;
  TypeTable types;
  return analyze(*prog, types, d) && !d.hasErrors();
}

bool hayMensaje(const Diagnostics& d, const std::string& fragmento) {
  for (const Diagnostic& item : d.all()) {
    if (item.message.find(fragmento) != std::string::npos) return true;
  }
  return false;
}

}  // namespace

// ============================================================== Parser

TEST(ConcurrenciaParser, HiloMarcaLaLlamada) {
  Diagnostics d;
  auto prog = parseProgram("t.lg", "fn f(a: int) -> int:\n    return a\nh = hilo f(1)\n", d);
  ASSERT_FALSE(d.hasErrors()) << errores(d);
  ASSERT_EQ(prog->stmts.size(), 1u);
  const auto& as = static_cast<const Assign&>(*prog->stmts[0]);
  ASSERT_EQ(as.value->kind, ExprKind::Call);
  EXPECT_TRUE(static_cast<const Call&>(*as.value).spawn);
}

TEST(ConcurrenciaParser, HiloSoloAdmiteLlamadas) {
  Diagnostics d;
  parseProgram("t.lg", "x = 1\nh = hilo x\n", d);
  EXPECT_TRUE(hayMensaje(d, "'hilo' solo admite una llamada a función")) << errores(d);
}

TEST(ConcurrenciaParser, AwaitEsUnaExpresion) {
  Diagnostics d;
  auto prog = parseProgram("t.lg", "async fn f() -> int:\n    return 1\nx = await f()\n", d);
  ASSERT_FALSE(d.hasErrors()) << errores(d);
  const auto& as = static_cast<const Assign&>(*prog->stmts[0]);
  EXPECT_EQ(as.value->kind, ExprKind::Await);
}

TEST(ConcurrenciaParser, AsyncFnSeMarca) {
  Diagnostics d;
  auto prog = parseProgram("t.lg", "async fn f() -> int:\n    return 1\n", d);
  ASSERT_FALSE(d.hasErrors()) << errores(d);
  ASSERT_EQ(prog->decls.size(), 1u);
  EXPECT_TRUE(static_cast<const FnDecl&>(*prog->decls[0]).isAsync);
}

TEST(ConcurrenciaParser, AsyncSoloANivelSuperior) {
  Diagnostics d;
  parseProgram("t.lg", "fn g():\n    async fn f() -> int:\n        return 1\n", d);
  EXPECT_TRUE(d.hasErrors());
}

TEST(ConcurrenciaParser, CanalLlevaSuTipo) {
  Diagnostics d;
  auto prog = parseProgram("t.lg", "c = Canal[int]()\n", d);
  ASSERT_FALSE(d.hasErrors()) << errores(d);
  const auto& as = static_cast<const Assign&>(*prog->stmts[0]);
  ASSERT_EQ(as.value->kind, ExprKind::Call);
  const auto& c = static_cast<const Call&>(*as.value);
  ASSERT_EQ(c.typeArgs.size(), 1u);
  EXPECT_EQ(c.typeArgs[0]->kind, TypeExpr::Kind::Name);
  EXPECT_EQ(c.typeArgs[0]->name, "int");
}

TEST(ConcurrenciaParser, TiposHiloCanalDebil) {
  Diagnostics d;
  parseProgram("t.lg",
               "fn f(a: Hilo[int], b: Canal[str], c: Debil[int], d: Option[Debil[int]]) -> int:\n    return 1\n",
               d);
  EXPECT_FALSE(d.hasErrors()) << errores(d);
}

// ============================================================== Sema

TEST(ConcurrenciaSema, ProgramaCorrectoSinErrores) {
  Diagnostics d;
  const char* src =
      "fn suma(a: int, b: int) -> int:\n"
      "    return a + b\n"
      "async fn doble(x: int) -> int:\n"
      "    return x * 2\n"
      "h = hilo suma(1, 2)\n"
      "print(h.esperar())\n"
      "c = Canal[int]()\n"
      "c.enviar(3)\n"
      "c.cerrar()\n"
      "r = c.recibir()\n"
      "print(r.es_some())\n"
      "print(await doble(4))\n"
      "v = debil(h)\n"
      "print(v.fuerte().es_some())\n";
  EXPECT_TRUE(analiza(src, d)) << errores(d);
}

TEST(ConcurrenciaSema, HiloRequiereFuncionDeNivelSuperior) {
  Diagnostics d;
  const char* src =
      "fn f() -> int:\n"
      "    fn g() -> int:\n"
      "        return 1\n"
      "    h = hilo g()\n"
      "    return 0\n";
  EXPECT_FALSE(analiza(src, d));
  EXPECT_TRUE(hayMensaje(d, "'hilo' solo admite funciones de nivel superior")) << errores(d);
}

TEST(ConcurrenciaSema, HiloRequiereValorDeRetorno) {
  Diagnostics d;
  EXPECT_FALSE(analiza("fn f():\n    print(1)\nh = hilo f()\n", d));
  EXPECT_TRUE(hayMensaje(d, "'hilo' requiere una función que devuelva un valor")) << errores(d);
}

TEST(ConcurrenciaSema, HiloCompruebaLosArgumentos) {
  Diagnostics d;
  EXPECT_FALSE(analiza("fn f(a: int) -> int:\n    return a\nh = hilo f(\"x\")\n", d));
  EXPECT_TRUE(hayMensaje(d, "tipo esperado int")) << errores(d);
}

TEST(ConcurrenciaSema, AwaitSoloDeHilo) {
  Diagnostics d;
  EXPECT_FALSE(analiza("x = 1\nprint(await x)\n", d));
  EXPECT_TRUE(hayMensaje(d, "'await' solo admite un Hilo[T]")) << errores(d);
}

TEST(ConcurrenciaSema, AsyncFnNecesitaRetorno) {
  Diagnostics d;
  EXPECT_FALSE(analiza("async fn f():\n    print(1)\n", d));
  EXPECT_TRUE(hayMensaje(d, "necesita un tipo de retorno")) << errores(d);
}

TEST(ConcurrenciaSema, DebilSoloDeValoresHeap) {
  Diagnostics d;
  EXPECT_FALSE(analiza("x = debil(3)\n", d));
  EXPECT_TRUE(hayMensaje(d, "debil() no admite int")) << errores(d);
}

TEST(ConcurrenciaSema, CanalTipaLosMensajes) {
  Diagnostics d;
  EXPECT_FALSE(analiza("c = Canal[int]()\nc.enviar(\"x\")\n", d));
  EXPECT_TRUE(hayMensaje(d, "tipo esperado int")) << errores(d);
}

TEST(ConcurrenciaSema, CanalSinTipoEsError) {
  Diagnostics d;
  EXPECT_FALSE(analiza("c = Canal()\n", d));
  EXPECT_TRUE(hayMensaje(d, "Canal necesita el tipo")) << errores(d);
}

TEST(ConcurrenciaSema, FuerteDevuelveOption) {
  Diagnostics d;
  EXPECT_TRUE(analiza("struct P:\n    v: int\nmut p = P(v: 1)\nd = debil(p)\nx: int = 1\n"
                      "if d.fuerte().es_some():\n    print(x)\n",
                      d))
      << errores(d);
}

TEST(ConcurrenciaSema, NombresReservados) {
  Diagnostics d;
  EXPECT_FALSE(analiza("fn debil(x: int) -> int:\n    return x\n", d));
  EXPECT_TRUE(d.hasErrors());
}
