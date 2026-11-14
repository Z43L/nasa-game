// lgpm: gestor de paquetes del lenguaje.
//   lgpm nuevo NOMBRE                          crea NOMBRE/lg.toml y NOMBRE/src/main.lg
//   lgpm resolver                              resuelve dependencias y escribe lg.lock
//   lgpm construir                             llama a lgc con las rutas de dependencias -> build/NOMBRE
//   lgpm ejecutar [args...]                    construir y luego ejecutar build/NOMBRE
//   lgpm añadir NOMBRE --ruta R                añade una dependencia por ruta a lg.toml
//   lgpm añadir NOMBRE --git URL [--rev X]     añade una dependencia git a lg.toml
//
// Cada dependencia aporta una carpeta de búsqueda para lgc (-I): <raiz>/src si existe, o <raiz>.
// Las dependencias git se clonan en ~/.lgpm/cache y se fija la revisión con `git checkout`.
// Variable LGPM_LGC: ruta de lgc a usar (por defecto, la que se compiló con esta herramienta).
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "toml.h"

#ifndef LGPM_LGC_DEFAULT
#error "LGPM_LGC_DEFAULT debe definirlo CMake"
#endif

namespace fs = std::filesystem;
using lgpm::TomlTable;
using lgpm::TomlValue;

namespace {

// Error que se muestra al usuario (uso incorrecto, datos inválidos, fallo de git...).
struct CliError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

std::string shq(const std::string& s) {
  std::string out = "'";
  for (char c : s) {
    if (c == '\'') out += "'\\''";
    else out += c;
  }
  return out + "'";
}

// Ejecuta un comando del shell y devuelve su código de salida (-1 si no se pudo ejecutar).
int run(const std::string& cmd) {
  int st = std::system(cmd.c_str());
  if (st == -1) return -1;
  return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

// Ejecuta un comando y devuelve su salida estándar sin espacios finales.
std::string capture(const std::string& cmd) {
  std::string out;
  FILE* p = popen(cmd.c_str(), "r");
  if (!p) return out;
  char buf[256];
  while (size_t n = fread(buf, 1, sizeof buf, p)) out.append(buf, n);
  pclose(p);
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
  return out;
}

std::string fnv1a(const std::string& s) {
  uint64_t h = 1469598103934665603ULL;
  for (unsigned char c : s) {
    h ^= c;
    h *= 1099511628211ULL;
  }
  char buf[17];
  std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
  return buf;
}

bool nombreValido(const std::string& n) {
  if (n.empty()) return false;
  for (char c : n) {
    bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-';
    if (!ok) return false;
  }
  return true;
}

std::string leerTexto(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) throw CliError("no se puede leer '" + p.string() + "'");
  std::stringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void escribirTexto(const fs::path& p, const std::string& text) {
  std::ofstream out(p, std::ios::binary);
  if (!out) throw CliError("no se puede escribir '" + p.string() + "'");
  out << text;
}

// ---------- Manifiesto lg.toml ----------

struct Dep {
  std::string nombre, ruta, git, rev;
};

struct Manifest {
  fs::path dir;  // carpeta del proyecto (la que contiene lg.toml)
  std::string nombre, version;
  std::vector<Dep> deps;
};

const TomlValue* buscar(const TomlTable& t, const std::string& clave) {
  auto it = t.find(clave);
  return it == t.end() ? nullptr : &it->second;
}

std::string textoDe(const TomlTable& t, const std::string& clave, const std::string& contexto) {
  const TomlValue* v = buscar(t, clave);
  if (!v) return "";
  if (v->kind != TomlValue::Kind::String)
    throw CliError(contexto + ": '" + clave + "' debe ser una cadena entre comillas");
  return v->str;
}

Manifest leerManifest(const fs::path& dir) {
  fs::path file = dir / "lg.toml";
  TomlTable t;
  try {
    t = lgpm::parseToml(leerTexto(file));
  } catch (lgpm::TomlError& e) {
    throw CliError(file.string() + ": " + e.what());
  }

  Manifest m;
  m.dir = dir;
  const TomlValue* proy = buscar(t, "proyecto");
  if (!proy || proy->kind != TomlValue::Kind::Table)
    throw CliError(file.string() + ": falta la tabla [proyecto]");
  m.nombre = textoDe(proy->table, "nombre", file.string() + " [proyecto]");
  if (!nombreValido(m.nombre))
    throw CliError(file.string() + " [proyecto]: falta 'nombre' o no es válido (letras, cifras, '_' o '-')");
  m.version = textoDe(proy->table, "version", file.string() + " [proyecto]");
  if (m.version.empty()) m.version = "0.0.0";

  const TomlValue* deps = buscar(t, "dependencias");
  if (deps) {
    if (deps->kind != TomlValue::Kind::Table) throw CliError(file.string() + ": [dependencias] debe ser una tabla");
    for (auto& [nombre, valor] : deps->table) {
      std::string ctx = file.string() + " [dependencias] " + nombre;
      if (!nombreValido(nombre)) throw CliError(ctx + ": nombre de dependencia no válido");
      if (valor.kind != TomlValue::Kind::Table)
        throw CliError(ctx + ": debe ser una tabla en línea, p. ej. { ruta = \"../" + nombre + "\" }");
      for (auto& [clave, valorClave] : valor.table) {
        if (clave != "ruta" && clave != "git" && clave != "rev")
          throw CliError(ctx + ": clave desconocida '" + clave + "' (válidas: ruta, git, rev)");
      }
      Dep d;
      d.nombre = nombre;
      d.ruta = textoDe(valor.table, "ruta", ctx);
      d.git = textoDe(valor.table, "git", ctx);
      d.rev = textoDe(valor.table, "rev", ctx);
      if (d.ruta.empty() == d.git.empty())
        throw CliError(ctx + ": indica 'ruta' o 'git' (exactamente una)");
      if (!d.rev.empty() && d.git.empty()) throw CliError(ctx + ": 'rev' solo se usa con 'git'");
      m.deps.push_back(d);
    }
  }
  return m;
}

// Sube desde `desde` hasta encontrar un directorio con lg.toml.
fs::path buscarProyecto(const fs::path& desde) {
  fs::path p = fs::absolute(desde);
  while (true) {
    if (fs::exists(p / "lg.toml")) return p;
    if (!p.has_relative_path() || p.parent_path() == p) break;
    p = p.parent_path();
  }
  throw CliError("no se encuentra lg.toml en esta carpeta ni en las superiores (usa 'lgpm nuevo')");
}

// ---------- Resolución de dependencias ----------

struct Resuelta {
  std::string nombre;
  fs::path raiz;   // carpeta del paquete (ruta o caché git)
  fs::path src;    // carpeta de búsqueda de imports para lgc (-I)
  bool esGit = false;
  std::string origen;  // ruta absoluta (dep por ruta) o URL git
  std::string rev, commit;
};

fs::path carpetaFuente(const fs::path& raiz) {
  fs::path s = raiz / "src";
  std::error_code ec;
  return fs::is_directory(s, ec) ? s : raiz;
}

// ~/.lgpm/cache/<nombre>-<hash(url, rev)>
fs::path dirCache(const std::string& url, const std::string& rev) {
  const char* home = std::getenv("HOME");
  if (!home) throw CliError("no está definida la variable HOME (necesaria para ~/.lgpm/cache)");
  std::string base = url;
  while (!base.empty() && base.back() == '/') base.pop_back();
  if (base.size() > 4 && base.compare(base.size() - 4, 4, ".git") == 0) base.resize(base.size() - 4);
  size_t slash = base.find_last_of("/:");
  std::string name = slash == std::string::npos ? base : base.substr(slash + 1);
  for (char& c : name)
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') c = '_';
  if (name.empty()) name = "paquete";
  return fs::path(home) / ".lgpm" / "cache" / (name + "-" + fnv1a(url + "\n" + rev));
}

Resuelta resolverDep(const Manifest& m, const Dep& d) {
  Resuelta r;
  r.nombre = d.nombre;
  if (!d.ruta.empty()) {
    fs::path raiz = fs::weakly_canonical(m.dir / d.ruta);
    std::error_code ec;
    if (!fs::is_directory(raiz, ec))
      throw CliError("la dependencia '" + d.nombre + "' apunta a '" + raiz.string() +
                     "', que no es una carpeta. Corrige 'ruta' en lg.toml o usa 'lgpm añadir'");
    r.raiz = raiz;
    r.src = carpetaFuente(raiz);
    r.origen = raiz.string();
    return r;
  }

  fs::path cache = dirCache(d.git, d.rev);
  std::error_code ec;
  if (!fs::is_directory(cache, ec)) {
    fs::create_directories(cache.parent_path());
    if (run("git clone --quiet " + shq(d.git) + " " + shq(cache.string())) != 0) {
      fs::remove_all(cache, ec);
      throw CliError("no se pudo clonar '" + d.git + "' para la dependencia '" + d.nombre +
                     "'. Comprueba la URL y el acceso a la red");
    }
  }
  if (!d.rev.empty()) {
    std::string co = "git -C " + shq(cache.string()) + " checkout --quiet " + shq(d.rev);
    if (run(co + " 2>/dev/null") != 0) {
      // La revisión puede ser nueva en el remoto: se actualiza la caché una vez y se reintenta.
      run("git -C " + shq(cache.string()) + " fetch --quiet --tags origin 2>/dev/null");
      if (run(co) != 0)
        throw CliError("no existe la revisión '" + d.rev + "' en '" + d.git + "' (dependencia '" + d.nombre +
                       "'). Comprueba el nombre de la etiqueta, rama o commit en lg.toml");
    }
  }
  r.raiz = cache;
  r.src = carpetaFuente(cache);
  r.origen = d.git;
  r.esGit = true;
  r.rev = d.rev;
  r.commit = capture("git -C " + shq(cache.string()) + " rev-parse HEAD");
  return r;
}

std::vector<Resuelta> resolverTodas(const Manifest& m) {
  std::vector<Resuelta> out;
  for (auto& d : m.deps) out.push_back(resolverDep(m, d));
  return out;
}

std::string lgcPath() {
  if (const char* env = std::getenv("LGPM_LGC")) return env;
  return LGPM_LGC_DEFAULT;
}

// Compila el proyecto y devuelve la ruta del ejecutable generado.
fs::path construir(const Manifest& m) {
  fs::path main = m.dir / "src" / "main.lg";
  std::error_code ec;
  if (!fs::is_regular_file(main, ec))
    throw CliError("falta el archivo de entrada '" + main.string() + "'");

  std::vector<Resuelta> rs = resolverTodas(m);
  fs::path build = m.dir / "build";
  fs::create_directories(build);
  fs::path exe = build / m.nombre;

  std::string cmd = shq(lgcPath()) + " " + shq(main.string()) + " -o " + shq(exe.string());
  for (auto& r : rs) cmd += " -I " + shq(r.src.string());
  std::cerr << "lgpm: compilando " << m.nombre << " " << m.version << " con " << rs.size()
            << " dependencia(s)\n";
  if (run(cmd) != 0) throw CliError("falló la compilación de '" + m.nombre + "'");
  std::cerr << "lgpm: construido " << fs::relative(exe, m.dir).string() << "\n";
  return exe;
}

// ---------- Comandos ----------

int cmdNuevo(const std::vector<std::string>& args) {
  if (args.size() != 1) throw CliError("uso: lgpm nuevo NOMBRE");
  const std::string& nombre = args[0];
  if (!nombreValido(nombre)) throw CliError("nombre de proyecto no válido: '" + nombre + "' (letras, cifras, '_' o '-')");
  fs::path dir = fs::current_path() / nombre;
  std::error_code ec;
  if (fs::exists(dir, ec)) throw CliError("ya existe '" + nombre + "'");
  fs::create_directories(dir / "src");
  escribirTexto(dir / "lg.toml",
                "[proyecto]\nnombre = " + lgpm::tomlQuote(nombre) + "\nversion = \"0.1.0\"\n\n[dependencias]\n");
  escribirTexto(dir / "src" / "main.lg", "print(\"Hola desde " + nombre + "\")\n");
  std::cout << "proyecto '" << nombre << "' creado en " << nombre << "/\n";
  std::cout << "  construir: cd " << nombre << " && lgpm construir\n";
  return 0;
}

int cmdResolver() {
  fs::path dir = buscarProyecto(fs::current_path());
  Manifest m = leerManifest(dir);
  std::vector<Resuelta> rs = resolverTodas(m);

  std::string lock = "# lg.lock generado por lgpm. No editar a mano.\n";
  for (auto& r : rs) {
    lock += "\n[" + r.nombre + "]\n";
    if (!r.esGit) {
      lock += "ruta = " + lgpm::tomlQuote(r.raiz.string()) + "\n";
    } else {
      lock += "git = " + lgpm::tomlQuote(r.origen) + "\n";
      if (!r.rev.empty()) lock += "rev = " + lgpm::tomlQuote(r.rev) + "\n";
      if (!r.commit.empty()) lock += "commit = " + lgpm::tomlQuote(r.commit) + "\n";
      lock += "ruta = " + lgpm::tomlQuote(r.raiz.string()) + "\n";
    }
    std::cout << "  " << r.nombre << " -> " << r.raiz.string() << "\n";
  }
  escribirTexto(dir / "lg.lock", lock);
  std::cout << "lg.lock escrito (" << rs.size() << " dependencia(s))\n";
  return 0;
}

int cmdConstruir() {
  Manifest m = leerManifest(buscarProyecto(fs::current_path()));
  construir(m);
  return 0;
}

int cmdEjecutar(const std::vector<std::string>& args) {
  Manifest m = leerManifest(buscarProyecto(fs::current_path()));
  fs::path exe = construir(m);
  std::string cmd = shq(exe.string());
  for (auto& a : args) cmd += " " + shq(a);
  int rc = run(cmd);
  return rc < 0 ? 1 : rc;
}

int cmdAnadir(const std::vector<std::string>& args) {
  const std::string uso = "uso: lgpm añadir NOMBRE --ruta R | --git URL [--rev X]";
  if (args.empty()) throw CliError(uso);
  const std::string nombre = args[0];
  if (!nombreValido(nombre)) throw CliError("nombre de dependencia no válido: '" + nombre + "'");

  std::string ruta, git, rev;
  bool tieneRev = false;
  for (size_t i = 1; i < args.size(); ++i) {
    const std::string& a = args[i];
    if (a == "--ruta" && i + 1 < args.size()) ruta = args[++i];
    else if (a == "--git" && i + 1 < args.size()) git = args[++i];
    else if (a == "--rev" && i + 1 < args.size()) { rev = args[++i]; tieneRev = true; }
    else throw CliError("opción no reconocida '" + a + "'. " + uso);
  }
  if (ruta.empty() == git.empty()) throw CliError("indica --ruta o --git (exactamente una). " + uso);
  if (tieneRev && git.empty()) throw CliError("--rev solo se usa con --git");

  fs::path dir = buscarProyecto(fs::current_path());
  Manifest m = leerManifest(dir);
  for (auto& d : m.deps)
    if (d.nombre == nombre) throw CliError("ya existe la dependencia '" + nombre + "' en lg.toml");

  std::string linea;
  if (!ruta.empty()) {
    // La ruta se da desde la carpeta actual; en lg.toml se guarda relativa al proyecto.
    fs::path abs = fs::weakly_canonical(fs::current_path() / ruta);
    std::error_code ec;
    if (!fs::is_directory(abs, ec)) throw CliError("'" + abs.string() + "' no es una carpeta");
    std::string rel = fs::relative(abs, fs::weakly_canonical(dir)).generic_string();
    if (rel.empty()) rel = ".";
    linea = nombre + " = { ruta = " + lgpm::tomlQuote(rel) + " }";
  } else {
    linea = nombre + " = { git = " + lgpm::tomlQuote(git);
    if (tieneRev) linea += ", rev = " + lgpm::tomlQuote(rev);
    linea += " }";
  }

  // Edición de texto: se conservan comentarios y formato del resto del archivo.
  fs::path file = dir / "lg.toml";
  std::string texto = leerTexto(file);
  std::vector<std::string> lineas;
  {
    std::istringstream ss(texto);
    std::string l;
    while (std::getline(ss, l)) lineas.push_back(l);
  }
  bool insertada = false;
  for (size_t i = 0; i < lineas.size(); ++i) {
    std::string t = lineas[i];
    while (!t.empty() && (t.back() == ' ' || t.back() == '\r' || t.back() == '\t')) t.pop_back();
    if (t == "[dependencias]") {
      lineas.insert(lineas.begin() + static_cast<std::ptrdiff_t>(i) + 1, linea);
      insertada = true;
      break;
    }
  }
  if (!insertada) {
    lineas.push_back("");
    lineas.push_back("[dependencias]");
    lineas.push_back(linea);
  }
  std::string nuevo;
  for (auto& l : lineas) nuevo += l + "\n";

  // Comprobar que el resultado sigue siendo un lg.toml válido antes de escribir.
  try {
    lgpm::parseToml(nuevo);
  } catch (lgpm::TomlError& e) {
    throw CliError("no se puede añadir la dependencia: " + std::string(e.what()));
  }
  escribirTexto(file, nuevo);
  std::cout << "dependencia '" << nombre << "' añadida a " << file.string() << "\n";
  std::cout << "  " << linea << "\n";
  return 0;
}

// ---------- lgpm app: proyecto de ejemplo, construcción a wasm y servidor local ----------

// Plantilla de src/main.lg (la app de ejemplo). Usa lib/app: rutas, estado, primitivas y responsive.
const char* const kPlantillaMain = R"LG(# Aplicación creada con `lgpm app nueva`. Construir y levantar:
#   lgpm app construir
#   lgpm app levantar 8080
#
# Dos pantallas: "/" (inicio: un contador y un formulario) y "/detalle/:id". El contador es una global de
# estado (lib/app/estado.lg): conserva su valor al cambiar de pantalla. Los estilos vienen de lib/app
# (primitivas y responsive); la hoja <style> sale dentro de la vista.
#
# Exports que pide web/index.html (lgpm app construir los exporta):
#   vista() -> str, ruta_inicio() -> int, ruta_byte(b: int) -> int, ruta_cambio() -> int, evento(id: int) -> int

import estado
import estilo
import hoja
import html
import primitivas
import render
import responsive
import rutas

struct Tema:
    raiz: Estilo
    cabecera: Estilo
    marca: Estilo
    enlace_barra: Estilo
    enlace: Estilo
    menu: Estilo
    filas: Estilo
    tarjeta: Estilo
    titulo: Estilo
    prosa: Estilo
    cifra: Estilo
    primario: Estilo
    secundario: Estilo
    campo: Estilo
    acciones: Estilo

mut tabla_listas = configurar_rutas()
mut contador_id = estado_nuevo(0)
mut cambios_id = estado_nuevo(0)
mut actual: Coincidencia = Coincidencia(encontrada: false, pantalla: 404, parametros: [])

fn configurar_rutas() -> int:
    rutas_nuevas()
    rutas_agregar(ruta("/", 1))
    rutas_agregar(ruta("/detalle/:id", 2))
    return rutas_pantalla_404()

fn tema_nuevo() -> Tema:
    mut raiz = estilo_nuevo()
    raiz.fondo = "#f3f4f6"
    raiz.color = "#111827"
    raiz.relleno = "16px"
    raiz.ancho = unidad_pct(100)

    mut cabecera = estilo_nuevo()
    cabecera.fondo = "#1f2937"
    cabecera.color = "#ffffff"
    cabecera.relleno = "12px 16px"
    cabecera.radio = "12px"
    cabecera.direccion = "fila"
    cabecera.justificar = "entre"
    cabecera = responsive_columna_movil(cabecera)

    mut marca = estilo_nuevo()
    marca.tamano_letra = "1.4rem"
    marca.color = "#ffffff"

    mut enlace_barra = estilo_nuevo()
    enlace_barra.color = "#fde68a"
    enlace_barra.margen = "0 8px 0 0"

    mut enlace = estilo_nuevo()
    enlace.color = "#1d4ed8"
    enlace.margen = "0 8px 0 0"

    mut menu = estilo_nuevo()
    menu.direccion = "fila"
    menu.relleno = "4px 0"

    mut filas = estilo_nuevo()
    filas.direccion = "fila"
    filas = responsive_columna_movil(filas)

    mut tarjeta = estilo_nuevo()
    tarjeta.fondo = "#ffffff"
    tarjeta.relleno = "16px"
    tarjeta.radio = "12px"
    tarjeta.margen = "8px"
    tarjeta.direccion = "columna"
    tarjeta.ancho = unidad_pct(31)
    tarjeta = responsive_movil(tarjeta, "width", unidad_pct(100))
    tarjeta = responsive_tableta(tarjeta, "width", unidad_pct(47))

    mut titulo = estilo_nuevo()
    titulo.tamano_letra = "1.4rem"
    titulo.color = "#111827"
    titulo.margen = "0 0 8px 0"

    mut prosa = estilo_nuevo()
    prosa.tamano_letra = "1rem"
    prosa.color = "#374151"
    prosa.margen = "0 0 8px 0"

    mut cifra = estilo_nuevo()
    cifra.tamano_letra = unidad_rem(2.0)
    cifra.color = "#047857"
    cifra.margen = "8px 0"

    mut primario = estilo_nuevo()
    primario.fondo = "#2563eb"
    primario.color = "#ffffff"
    primario.relleno = "10px 16px"
    primario.radio = "8px"
    primario.tamano_letra = "1rem"
    primario.margen = "4px 8px 4px 0"

    mut secundario = estilo_nuevo()
    secundario.fondo = "#e5e7eb"
    secundario.color = "#111827"
    secundario.relleno = "10px 16px"
    secundario.radio = "8px"
    secundario.tamano_letra = "1rem"
    secundario.margen = "4px 8px 4px 0"

    mut campo = estilo_nuevo()
    campo.relleno = "10px"
    campo.radio = "8px"
    campo.tamano_letra = "1rem"
    campo.ancho = unidad_pct(100)
    campo.margen = "8px 0"

    mut acciones = estilo_nuevo()
    acciones.direccion = "fila"
    acciones.margen = "8px 0 0 0"

    return Tema(raiz: raiz, cabecera: cabecera, marca: marca, enlace_barra: enlace_barra, enlace: enlace,
                menu: menu, filas: filas, tarjeta: tarjeta, titulo: titulo, prosa: prosa, cifra: cifra,
                primario: primario, secundario: secundario, campo: campo, acciones: acciones)

# Todos los estilos de la vista: la hoja <style> lleva una regla por cada uno.
fn tema_lista(t: Tema) -> List[Estilo]:
    l: List[Estilo] = [t.raiz, t.cabecera, t.marca, t.enlace_barra, t.enlace, t.menu, t.filas, t.tarjeta,
                       t.titulo, t.prosa, t.cifra, t.primario, t.secundario, t.campo, t.acciones]
    return l

fn enlace_menu(e: Estilo, destino: str, etiqueta: str) -> Nodo:
    return ui_elemento("a", ["href", destino, "data-ruta", destino, "class", estilo_clase(e)], [ui_texto(etiqueta)])

fn cabecera_app(t: Tema) -> Nodo:
    return caja(t.cabecera, [
        texto(t.marca, "Mi app"),
        fila(t.menu, [
            enlace_menu(t.enlace_barra, "/", "Inicio"),
            enlace_menu(t.enlace_barra, "/detalle/42", "Detalle 42"),
            enlace_menu(t.enlace_barra, "/detalle/7", "Detalle 7")
        ])
    ])

fn tarjeta_contador(t: Tema) -> Nodo:
    return caja(t.tarjeta, [
        texto(t.titulo, "Contador"),
        texto(t.cifra, "Cuenta: " + str(estado_get(contador_id))),
        fila(t.acciones, [boton(t.primario, "+1", 1), boton(t.secundario, "Reiniciar", 2)])
    ])

fn tarjeta_formulario(t: Tema) -> Nodo:
    return caja(t.tarjeta, [
        texto(t.titulo, "Formulario"),
        texto(t.prosa, "Escribe un nombre. El texto se conserva al cambiar la vista."),
        ui_elemento("div", ["data-cambio", "3"], [entrada(t.campo, "nombre", "")]),
        texto(t.prosa, "Cambios en la entrada: " + str(estado_get(cambios_id)))
    ])

fn pantalla_inicio(t: Tema) -> Nodo:
    return columna(t.raiz, [
        texto(t.titulo, "Inicio"),
        fila(t.filas, [tarjeta_contador(t), tarjeta_formulario(t)])
    ])

fn pantalla_detalle(t: Tema, id: str) -> Nodo:
    return caja(t.tarjeta, [
        texto(t.titulo, "Detalle " + id),
        texto(t.prosa, "Parámetro id = " + id),
        texto(t.prosa, "El contador conserva su cuenta: " + str(estado_get(contador_id))),
        fila(t.acciones, [enlace_menu(t.enlace, "/", "Volver al inicio")])
    ])

fn pantalla_no_existe(t: Tema) -> Nodo:
    return caja(t.tarjeta, [
        texto(t.titulo, "No encontrada"),
        texto(t.prosa, "Esta ruta no existe.")
    ])

fn vista() -> str:
    t = tema_nuevo()
    mut cuerpo: List[Nodo] = []
    cuerpo.append(cabecera_app(t))
    if actual.pantalla == 1:
        cuerpo.append(pantalla_inicio(t))
    elif actual.pantalla == 2:
        cuerpo.append(pantalla_detalle(t, actual.parametros[0]))
    else:
        cuerpo.append(pantalla_no_existe(t))
    return hoja_css(tema_lista(t)) + ui_render(caja(t.raiz, cuerpo))

# Navegación: el puente pasa el path byte a byte (los exports solo reciben enteros).
fn ruta_inicio() -> int:
    return rutas_entrada_nueva()

fn ruta_byte(b: int) -> int:
    return rutas_entrada_byte(b)

fn ruta_cambio() -> int:
    actual = rutas_resolver(rutas_entrada_texto())
    return actual.pantalla

# Eventos: 1 y 2 mueven el contador; 3 cuenta los cambios de la entrada.
fn evento(id: int) -> int:
    if id == 1:
        return estado_sumar(contador_id, 1)
    if id == 2:
        return estado_reiniciar(contador_id)
    if id == 3:
        return estado_sumar(cambios_id, 1)
    return estado_get(contador_id)

actual = rutas_resolver("/")
)LG";

// Página de la app. __NOMBRE__ se sustituye por el nombre del proyecto al construir.
// Las rutas son absolutas (/...): la página funciona también en /detalle/7 al recargar.
const char* const kPlantillaIndex = R"HTML(<!doctype html>
<html lang="es">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
  <title>__NOMBRE__</title>
  <link rel="manifest" href="/manifest.webmanifest">
  <meta name="theme-color" content="#1f2937">
  <meta name="apple-mobile-web-app-capable" content="yes">
  <meta name="apple-mobile-web-app-status-bar-style" content="default">
  <meta name="apple-mobile-web-app-title" content="__NOMBRE__">
  <link rel="icon" href="/icons/icon-192.png" type="image/png">
  <link rel="apple-touch-icon" href="/icons/icon-192.png">
  <style>
    body { margin: 0; font-family: system-ui, sans-serif; background: #f3f4f6;
      padding-top: env(safe-area-inset-top); padding-right: env(safe-area-inset-right);
      padding-bottom: env(safe-area-inset-bottom); padding-left: env(safe-area-inset-left); }
  </style>
  <script src="/registro.js" data-sw="/sw.js"></script>
</head>
<body>
  <div id="app"><p>Cargando…</p></div>
  <noscript><p>Esta aplicación necesita JavaScript y WebAssembly.</p></noscript>
  <script src="/puente.js"></script>
  <script src="/app_puente.js"></script>
  <script>
    AppPuente.montar({ wasm: '/app.wasm', contenedor: document.getElementById('app') })
      .catch(function (e) { document.getElementById('app').textContent = 'Error: ' + e.message; });
  </script>
</body>
</html>
)HTML";

// Raíz del repositorio de Lenguaje (lib/app, lib/ui, stdlib). Por defecto, la de la compilación de lgpm;
// LGPM_RAIZ la sobrescribe.
fs::path raizLenguaje() {
  const char* env = std::getenv("LGPM_RAIZ");
  fs::path raiz = env ? fs::path(env) : fs::path(LGPM_RAIZ_DEFAULT);
  std::error_code ec;
  if (!fs::is_regular_file(raiz / "lib" / "app" / "app_puente.js", ec))
    throw CliError("no se encuentra la raíz de Lenguaje en '" + raiz.string() +
                   "' (falta lib/app/app_puente.js). Define LGPM_RAIZ=/ruta/al/repositorio");
  return raiz;
}

// Compilador de wasm: LGPM_LGC, o el lgc que se compiló junto con lgpm.
fs::path lgcVerificado() {
  std::string p = lgcPath();
  if (access(p.c_str(), X_OK) != 0)
    throw CliError("no se encuentra el compilador lgc en '" + p +
                   "'. Define LGPM_LGC=/ruta/a/lgc (por defecto, el lgc compilado con lgpm)");
  return p;
}

void copiarArchivo(const fs::path& from, const fs::path& to) {
  std::error_code ec;
  fs::create_directories(to.parent_path(), ec);
  fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
  if (ec) throw CliError("no se puede copiar '" + from.string() + "' a '" + to.string() + "': " + ec.message());
}

void copiarCarpeta(const fs::path& from, const fs::path& to) {
  std::error_code ec;
  fs::create_directories(to, ec);
  fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
  if (ec) throw CliError("no se puede copiar la carpeta '" + from.string() + "': " + ec.message());
}

std::string reemplazarTodo(std::string s, const std::string& de, const std::string& a) {
  for (size_t p = 0; (p = s.find(de, p)) != std::string::npos; p += a.size()) s.replace(p, de.size(), a);
  return s;
}

// Manifest de la app: start_url y scope relativos a la raíz de dist/ (donde vive el manifest).
std::string manifiestoApp(const std::string& nombre) {
  return "{\n"
         "  \"name\": \"" + nombre + "\",\n"
         "  \"short_name\": \"" + nombre + "\",\n"
         "  \"description\": \"Aplicación escrita en Lenguaje (WebAssembly), creada con lgpm app.\",\n"
         "  \"start_url\": \"./\",\n"
         "  \"scope\": \"./\",\n"
         "  \"display\": \"standalone\",\n"
         "  \"background_color\": \"#ffffff\",\n"
         "  \"theme_color\": \"#1f2937\",\n"
         "  \"lang\": \"es\",\n"
         "  \"icons\": [\n"
         "    {\"src\": \"icons/icon-192.png\", \"sizes\": \"192x192\", \"type\": \"image/png\", \"purpose\": \"any\"},\n"
         "    {\"src\": \"icons/icon-512.png\", \"sizes\": \"512x512\", \"type\": \"image/png\", \"purpose\": \"any\"}\n"
         "  ]\n"
         "}\n";
}

// Service worker de la app: el shell es lo que se cachea al instalar; el resto lo gestiona lg_sw.js
// (copia de lib/app/pwa/sw.js).
const char* const kSwApp =
    "self.LG_PWA_SHELL = [\"./\", \"index.html\", \"app.wasm\", \"puente.js\", \"app_puente.js\", "
    "\"registro.js\", \"manifest.webmanifest\", \"icons/icon-192.png\", \"icons/icon-512.png\"];\n"
    "importScripts(\"lg_sw.js\");\n";

// Exports que necesita la página de la plantilla (ver kPlantillaMain).
const std::vector<std::string> kExportsApp = {"vista", "ruta_inicio", "ruta_byte", "ruta_cambio", "evento"};

int cmdAppNueva(const std::vector<std::string>& args) {
  if (args.size() != 1) throw CliError("uso: lgpm app nueva NOMBRE");
  const std::string& nombre = args[0];
  if (!nombreValido(nombre))
    throw CliError("nombre de app no válido: '" + nombre + "' (letras, cifras, '_' o '-')");
  fs::path iconos = raizLenguaje() / "lib" / "app" / "pwa" / "icons";
  std::error_code ec;
  if (!fs::is_regular_file(iconos / "icon-192.png", ec) || !fs::is_regular_file(iconos / "icon-512.png", ec))
    throw CliError("faltan los iconos en '" + iconos.string() + "'");
  fs::path dir = fs::current_path() / nombre;
  if (fs::exists(dir, ec)) throw CliError("ya existe '" + nombre + "'");

  fs::create_directories(dir / "src");
  fs::create_directories(dir / "web" / "icons");
  escribirTexto(dir / "lg.toml",
                "[proyecto]\nnombre = " + lgpm::tomlQuote(nombre) + "\nversion = \"0.1.0\"\n\n[dependencias]\n");
  escribirTexto(dir / "src" / "main.lg", kPlantillaMain);
  escribirTexto(dir / "web" / "index.html", kPlantillaIndex);
  copiarArchivo(iconos / "icon-192.png", dir / "web" / "icons" / "icon-192.png");
  copiarArchivo(iconos / "icon-512.png", dir / "web" / "icons" / "icon-512.png");
  std::cout << "app '" << nombre << "' creada en " << nombre << "/\n"
            << "  construir: cd " << nombre << " && lgpm app construir\n"
            << "  levantar:  lgpm app levantar 8080\n";
  return 0;
}

int cmdAppConstruir() {
  fs::path dir = buscarProyecto(fs::current_path());
  Manifest m = leerManifest(dir);
  fs::path main = dir / "src" / "main.lg";
  fs::path web = dir / "web";
  std::error_code ec;
  if (!fs::is_regular_file(main, ec))
    throw CliError("falta '" + main.string() + "' (crea la app con 'lgpm app nueva')");
  if (!fs::is_regular_file(web / "index.html", ec) || !fs::is_directory(web / "icons", ec))
    throw CliError("falta web/index.html o web/icons/ en '" + dir.string() + "'");
  fs::path raiz = raizLenguaje();
  fs::path lgc = lgcVerificado();
  std::vector<Resuelta> rs = resolverTodas(m);

  // 1. Compilación a wasm en build/app.wasm.
  fs::path build = dir / "build";
  fs::create_directories(build);
  fs::path wasm = build / "app.wasm";
  std::string cmd = "env -u LG_PATH " + shq(lgc.string()) + " " + shq(main.string()) + " --target wasm32";
  for (auto& e : kExportsApp) cmd += " --export " + e;
  cmd += " -I " + shq((raiz / "lib" / "app").string()) + " -I " + shq((raiz / "lib" / "ui").string()) +
         " -I " + shq((raiz / "stdlib").string()) + " -I " + shq((raiz / "lib" / "nucleo").string()) +
         " -I " + shq((dir / "src").string());
  for (auto& r : rs) cmd += " -I " + shq(r.src.string());
  cmd += " -o " + shq(wasm.string());
  std::cerr << "lgpm: compilando " << m.nombre << " a wasm32 con " << lgc.string() << "\n";
  int rc = run(cmd);
  if (rc < 0) throw CliError("no se pudo ejecutar el compilador '" + lgc.string() + "'");
  if (rc != 0)
    throw CliError("falló la compilación a wasm de '" + m.nombre + "' (código " + std::to_string(rc) +
                   "). Revisa los errores de arriba");

  // 2. dist/: página, manifest, service worker, iconos, puentes y el .wasm. Rutas absolutas en la página.
  fs::path dist = dir / "dist";
  fs::remove_all(dist, ec);
  fs::create_directories(dist);
  copiarArchivo(wasm, dist / "app.wasm");
  escribirTexto(dist / "index.html", reemplazarTodo(leerTexto(web / "index.html"), "__NOMBRE__", m.nombre));
  copiarCarpeta(web / "icons", dist / "icons");
  copiarArchivo(raiz / "lib" / "ui" / "puente.js", dist / "puente.js");
  copiarArchivo(raiz / "lib" / "app" / "app_puente.js", dist / "app_puente.js");
  copiarArchivo(raiz / "lib" / "app" / "pwa" / "registro.js", dist / "registro.js");
  copiarArchivo(raiz / "lib" / "app" / "pwa" / "sw.js", dist / "lg_sw.js");
  escribirTexto(dist / "sw.js", kSwApp);
  escribirTexto(dist / "manifest.webmanifest", manifiestoApp(m.nombre));
  std::cerr << "lgpm: construido " << fs::relative(dist, dir).string() << "/ (index.html, app.wasm, manifest, sw.js, iconos)\n";
  return 0;
}

// ---------- Servidor estático local (sockets POSIX) ----------

volatile std::sig_atomic_t g_parar = 0;
void alParar(int) { g_parar = 1; }

std::string tipoMime(const fs::path& p) {
  std::string e = p.extension().string();
  for (char& c : e) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (e == ".html" || e == ".htm") return "text/html; charset=utf-8";
  if (e == ".js" || e == ".mjs") return "text/javascript; charset=utf-8";
  if (e == ".wasm") return "application/wasm";
  if (e == ".webmanifest") return "application/manifest+json";
  if (e == ".json") return "application/json; charset=utf-8";
  if (e == ".css") return "text/css; charset=utf-8";
  if (e == ".png") return "image/png";
  if (e == ".jpg" || e == ".jpeg") return "image/jpeg";
  if (e == ".svg") return "image/svg+xml";
  if (e == ".ico") return "image/x-icon";
  if (e == ".txt") return "text/plain; charset=utf-8";
  return "application/octet-stream";
}

// Decodifica %XX. Devuelve false si un '%' no va seguido de dos dígitos hexadecimales.
bool decodificarRuta(const std::string& in, std::string& out) {
  out.clear();
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] != '%') {
      out += in[i];
      continue;
    }
    if (i + 2 >= in.size() || !std::isxdigit(static_cast<unsigned char>(in[i + 1])) ||
        !std::isxdigit(static_cast<unsigned char>(in[i + 2])))
      return false;
    out += static_cast<char>(std::stoi(in.substr(i + 1, 2), nullptr, 16));
    i += 2;
  }
  return true;
}

// ¿Está p (ya existente) dentro de la carpeta raiz (canónica)? Evita salir por enlaces simbólicos.
bool dentroDe(const fs::path& raiz, const fs::path& p) {
  std::error_code ec;
  fs::path c = fs::weakly_canonical(p, ec);
  if (ec) return false;
  std::string r = raiz.string(), s = c.string();
  return s.size() > r.size() && s.compare(0, r.size(), r) == 0 && s[r.size()] == '/';
}

std::string respuestaHttp(int codigo, const char* razon, const std::string& tipo, const std::string& cuerpo,
                          bool soloCabeceras, const std::string& extra) {
  std::string h = "HTTP/1.1 " + std::to_string(codigo) + " " + razon + "\r\n" +
                  "Content-Type: " + tipo + "\r\n" +
                  "Content-Length: " + std::to_string(cuerpo.size()) + "\r\n" +
                  "Cache-Control: no-cache\r\n" + extra + "Connection: close\r\n\r\n";
  return soloCabeceras ? h : h + cuerpo;
}

// Respuesta a una petición. Rutas con '..' -> 403; ruta inexistente sin extensión (p. ej. /detalle/7) ->
// index.html (la SPA resuelve la ruta); inexistente con extensión -> 404.
std::string responder(const std::string& metodo, const std::string& destino, const fs::path& raiz, int& codigo) {
  const bool cabeza = metodo == "HEAD";
  const std::string texto = "text/plain; charset=utf-8";
  std::string ruta = destino.substr(0, destino.find_first_of("?#"));
  std::string plano;
  if (ruta.empty() || ruta[0] != '/' || !decodificarRuta(ruta, plano) || plano.find('\0') != std::string::npos ||
      plano.find('\\') != std::string::npos) {
    codigo = 400;
    return respuestaHttp(400, "Bad Request", texto, "Ruta no válida\n", cabeza, "");
  }
  std::vector<std::string> segs;
  std::string seg;
  for (size_t i = 0; i <= plano.size(); ++i) {
    if (i == plano.size() || plano[i] == '/') {
      if (seg == "..") {
        codigo = 403;
        return respuestaHttp(403, "Forbidden", texto, "Prohibido\n", cabeza, "");
      }
      if (!seg.empty()) segs.push_back(seg);
      seg.clear();
    } else {
      seg += plano[i];
    }
  }
  fs::path candidato = raiz;
  for (auto& s : segs) candidato /= s;
  std::error_code ec;
  if (fs::is_directory(candidato, ec)) candidato /= "index.html";
  const bool ultimoConPunto = !segs.empty() && segs.back().find('.') != std::string::npos;

  fs::path archivo;
  if (fs::is_regular_file(candidato, ec) && dentroDe(raiz, candidato)) archivo = candidato;
  else if (!ultimoConPunto && fs::is_regular_file(raiz / "index.html", ec)) archivo = raiz / "index.html";
  else {
    codigo = 404;
    return respuestaHttp(404, "Not Found", texto, "No encontrado\n", cabeza, "");
  }
  std::ifstream in(archivo, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  codigo = 200;
  return respuestaHttp(200, "OK", tipoMime(archivo), ss.str(), cabeza, "");
}

void enviarTodo(int fd, const std::string& s) {
  size_t off = 0;
  while (off < s.size()) {
    ssize_t n = send(fd, s.data() + off, s.size() - off, MSG_NOSIGNAL);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) return;
    off += static_cast<size_t>(n);
  }
}

void atender(int cli, const fs::path& raiz) {
  timeval tv{5, 0};
  setsockopt(cli, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  std::string req;
  char buf[2048];
  while (req.find("\r\n\r\n") == std::string::npos && req.size() < 16384) {
    ssize_t n = recv(cli, buf, sizeof buf, 0);
    if (n <= 0) break;
    req.append(buf, static_cast<size_t>(n));
  }
  std::istringstream linea(req.substr(0, req.find("\r\n")));
  std::string metodo, destino;
  linea >> metodo >> destino;
  int codigo = 400;
  std::string resp;
  if (metodo != "GET" && metodo != "HEAD" && !metodo.empty()) {
    codigo = 405;
    resp = respuestaHttp(405, "Method Not Allowed", "text/plain; charset=utf-8", "Método no permitido\n", false,
                         "Allow: GET, HEAD\r\n");
  } else if (metodo.empty() || destino.empty()) {
    resp = respuestaHttp(400, "Bad Request", "text/plain; charset=utf-8", "Petición no válida\n", false, "");
  } else {
    resp = responder(metodo, destino, raiz, codigo);
  }
  enviarTodo(cli, resp);
  std::cout << codigo << " " << (metodo.empty() ? "-" : metodo) << " " << destino << std::endl;
}

// IPv4 de la red local (la primera interfaz activa que no sea loopback, con preferencia por en*/eth*/wl*).
std::string ipRed() {
  ifaddrs* lista = nullptr;
  if (getifaddrs(&lista) != 0) return "";
  std::string ip, preferida;
  for (ifaddrs* it = lista; it; it = it->ifa_next) {
    if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET) continue;
    if (!(it->ifa_flags & IFF_UP) || (it->ifa_flags & IFF_LOOPBACK)) continue;
    char b[INET_ADDRSTRLEN];
    auto* sa = reinterpret_cast<sockaddr_in*>(it->ifa_addr);
    inet_ntop(AF_INET, &sa->sin_addr, b, sizeof b);
    std::string nombre = it->ifa_name ? it->ifa_name : "";
    if (ip.empty()) ip = b;
    if (preferida.empty() && (nombre.rfind("en", 0) == 0 || nombre.rfind("eth", 0) == 0 || nombre.rfind("wl", 0) == 0))
      preferida = b;
  }
  freeifaddrs(lista);
  return preferida.empty() ? ip : preferida;
}

int cmdAppLevantar(const std::vector<std::string>& args) {
  const std::string uso = "uso: lgpm app levantar [puerto]";
  if (args.size() > 1) throw CliError(uso);
  int puerto = 8080;
  if (args.size() == 1) {
    const std::string& p = args[0];
    bool digitos = !p.empty() && p.size() <= 5;
    for (char c : p) digitos = digitos && std::isdigit(static_cast<unsigned char>(c));
    if (!digitos || std::stoi(p) < 1 || std::stoi(p) > 65535)
      throw CliError("puerto no válido: '" + p + "' (debe estar entre 1 y 65535)");
    puerto = std::stoi(p);
  }
  fs::path dist = buscarProyecto(fs::current_path()) / "dist";
  std::error_code ec;
  if (!fs::is_regular_file(dist / "index.html", ec))
    throw CliError("no hay dist/index.html: ejecuta 'lgpm app construir' primero");
  fs::path raiz = fs::canonical(dist, ec);

  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) throw CliError(std::string("no se puede crear el socket: ") + std::strerror(errno));
  int si = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &si, sizeof si);
  sockaddr_in a{};
  a.sin_family = AF_INET;
  a.sin_port = htons(static_cast<uint16_t>(puerto));
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0) {
    int e = errno;
    close(s);
    if (e == EADDRINUSE)
      throw CliError("el puerto " + std::to_string(puerto) + " ya está en uso. Prueba con otro: lgpm app levantar " +
                     std::to_string(puerto + 1));
    throw CliError("no se puede usar el puerto " + std::to_string(puerto) + ": " + std::strerror(e));
  }
  if (listen(s, 16) < 0) {
    int e = errno;
    close(s);
    throw CliError(std::string("no se puede escuchar: ") + std::strerror(e));
  }
  std::signal(SIGINT, alParar);
  std::signal(SIGTERM, alParar);
  std::signal(SIGPIPE, SIG_IGN);

  std::cout << "Sirviendo " << dist.string() << "\n"
            << "  Local: http://localhost:" << puerto << "/\n";
  std::string ip = ipRed();
  if (ip.empty()) std::cout << "  Red:   (no se encontró una IPv4 de red local)\n";
  else std::cout << "  Red:   http://" << ip << ":" << puerto << "/  (móvil en la misma red Wi-Fi)\n";
  std::cout << "Ctrl+C para parar." << std::endl;

  while (!g_parar) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(s, &fds);
    timeval tv{0, 500000};
    int r = select(s + 1, &fds, nullptr, nullptr, &tv);
    if (r < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (r == 0) continue;
    int cli = accept(s, nullptr, nullptr);
    if (cli < 0) continue;
    atender(cli, raiz);
    close(cli);
  }
  close(s);
  std::cout << "servidor detenido" << std::endl;
  return 0;
}

int cmdApp(const std::vector<std::string>& args) {
  const std::string uso = "uso: lgpm app nueva NOMBRE | lgpm app construir | lgpm app levantar [puerto]";
  if (args.empty()) throw CliError(uso);
  std::vector<std::string> resto(args.begin() + 1, args.end());
  const std::string& sub = args[0];
  if (sub == "nueva") return cmdAppNueva(resto);
  if (sub == "construir") {
    if (!resto.empty()) throw CliError("uso: lgpm app construir (sin argumentos)");
    return cmdAppConstruir();
  }
  if (sub == "levantar") return cmdAppLevantar(resto);
  throw CliError("subcomando de app desconocido: '" + sub + "'. " + uso);
}

void usage() {
  std::cerr << "uso: lgpm <comando>\n"
               "  nuevo NOMBRE                       crea un proyecto nuevo\n"
               "  resolver                           resuelve dependencias y escribe lg.lock\n"
               "  construir                          compila el proyecto con lgc -> build/NOMBRE\n"
               "  ejecutar [args...]                 construye y ejecuta el programa\n"
               "  añadir NOMBRE --ruta R             añade una dependencia por ruta\n"
               "  añadir NOMBRE --git URL [--rev X]  añade una dependencia git\n"
               "  app nueva NOMBRE                   crea una app web (wasm + PWA) de ejemplo\n"
               "  app construir                      compila src/main.lg a wasm y genera dist/\n"
               "  app levantar [puerto]              sirve dist/ en localhost (8080 por defecto)\n"
               "Variables: LGPM_LGC (ruta de lgc), LGPM_RAIZ (raíz del repositorio de Lenguaje).\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 2;
  }
  std::string cmd = argv[1];
  std::vector<std::string> args(argv + 2, argv + argc);
  try {
    if (cmd == "nuevo") return cmdNuevo(args);
    if (cmd == "resolver") return cmdResolver();
    if (cmd == "construir") return cmdConstruir();
    if (cmd == "ejecutar") return cmdEjecutar(args);
    if (cmd == "añadir" || cmd == "anadir") return cmdAnadir(args);
    if (cmd == "app") return cmdApp(args);
    if (cmd == "-h" || cmd == "--help") {
      usage();
      return 0;
    }
    std::cerr << "lgpm: comando desconocido '" << cmd << "'\n";
    usage();
    return 2;
  } catch (CliError& e) {
    std::cerr << "lgpm: error: " << e.what() << "\n";
    return 1;
  } catch (fs::filesystem_error& e) {
    std::cerr << "lgpm: error: " << e.what() << "\n";
    return 1;
  }
}
