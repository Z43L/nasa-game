#!/usr/bin/env python3
# Fuzzing y verificación de lib/ui (html.lg / render.lg) para tests/qa/ui/run_ui_qa.sh.
#
# Modos:
#   fuzz_escape.py generar <carpeta> [semilla] [cadenas]
#       Escribe en <carpeta>:
#         escape_fuzz.lg        `cadenas` llamadas ui_escapar con ASCII imprimible, caracteres de
#                               control y UTF-8 multibyte. Tras cada resultado imprime "@@FIN@@".
#         escape_fuzz.json      entradas (lista de cadenas) para la comprobación.
#         escape_1mb.lg         cadena de ~1 MB con todos los caracteres especiales (una sola llamada).
#         escape_1mb.json       la cadena de entrada.
#         arboles.lg            árboles aleatorios + casos hostiles + cadena de profundidad 1000,
#                               uno por registro, separados por "@@FIN@@".
#         arboles.json          especificación de cada árbol (para comprobar la salida).
#   fuzz_escape.py comprobar <carpeta> <salida_fuzz> <salida_1mb> <salida_arboles>
#       Compara las salidas reales del programa lgc con la referencia Python (html.escape con
#       quote=True) y con la especificación de los árboles (parser html.parser de la biblioteca
#       estándar). Sale con 0 si todo pasa; 1 si hay fallos; 2 por uso incorrecto.
#
# Convenciones de la comprobación del escape:
#   EXACTO         la salida es idéntica a html.escape(s, quote=True).
#   EQUIV_COMILLA  difiere solo en la forma de la comilla simple (&#39; o &apos; frente a &#x27;).
#                  Son equivalentes en HTML; se cuenta como correcto y se anota.
#   DIF_UTF8       los caracteres no ASCII salen como referencias numéricas (&#233;) en lugar de
#                  UTF-8 literal. Se cuenta como correcto y se anota (intencionalidad por confirmar).
#   FAIL           cualquier otra diferencia.
import html
import itertools
import json
import os
import random
import re
import sys
from html.parser import HTMLParser

SENTINEL = "@@FIN@@"
PARTES_ESPECIALES = ['<', '>', '&', '"', "'"]
PARTES_ENTIDAD = ["&amp;", "&lt;", "&#39;", "&#x27;", "&quot;", "&nbsp;", "&", "&;", "&#", "&#x", "&amp"]
PARTES_MULTI = ["é", "ñ", "中", "\U0001F600", " ", "́", "ß", "Ω"]
ETIQUETAS = ["div", "p", "span", "ul", "li", "em", "strong", "a"]
CLAVES = ["class", "title", "href", "id"]
VALORES_HOSTILES = [
    'x" onclick="alert(1)',
    "x' onmouseover='alert(1)",
    '"><script>alert(1)</script>',
    "javascript:alert(1)",
    "a&b<c>d",
    "&amp;",
    "",
    "plano",
]


def lit(s):
    """Literal de cadena de lg. Solo existen los escapes \\n \\r \\t \\\\ \\\" (docs/guia/06-cadenas.md)."""
    out = ['"']
    for c in s:
        if c == "\\":
            out.append("\\\\")
        elif c == '"':
            out.append('\\"')
        elif c == "\n":
            out.append("\\n")
        elif c == "\r":
            out.append("\\r")
        elif c == "\t":
            out.append("\\t")
        else:
            out.append(c)
    out.append('"')
    return "".join(out)


def cadena_aleatoria(rnd, maxtokens=24):
    partes = []
    for _ in range(rnd.randint(0, maxtokens)):
        r = rnd.random()
        if r < 0.35:
            partes.append(rnd.choice(PARTES_ESPECIALES))
        elif r < 0.60:
            partes.append(chr(rnd.randint(0x20, 0x7E)))
        elif r < 0.72:
            # Control: 0x01-0x1F y 0x7F. NUL excluido (no es representable de forma fiable).
            partes.append(chr(rnd.randint(0x01, 0x1F)) if rnd.random() < 0.9 else "\x7f")
        elif r < 0.82:
            partes.append(rnd.choice(PARTES_MULTI))
        elif r < 0.92:
            partes.append(rnd.choice(PARTES_ENTIDAD))
        else:
            partes.append(rnd.choice(["\n", "\r", "\t", "\\", '"']))
    return "".join(partes)


def cadena_1mb():
    # Ocho bytes UTF-8 por bloque: < > & " ' é (2 bytes) y 0x01. 125000 bloques = 1 000 000 bytes.
    bloque = '<>&"\'é\x01'
    return bloque * 125000


def escribir(ruta, texto):
    with open(ruta, "w", encoding="utf-8", newline="\n") as f:
        f.write(texto)


def generar_escape_fuzz(carpeta, semilla, n):
    rnd = random.Random(semilla)
    entradas = []
    while len(entradas) < n:
        s = cadena_aleatoria(rnd)
        if SENTINEL not in s:
            entradas.append(s)
    lineas = ["import html"]
    for s in entradas:
        lineas.append("print(ui_escapar(%s))" % lit(s))
        lineas.append('print("%s")' % SENTINEL)
    escribir(os.path.join(carpeta, "escape_fuzz.lg"), "\n".join(lineas) + "\n")
    with open(os.path.join(carpeta, "escape_fuzz.json"), "w", encoding="utf-8") as f:
        json.dump(entradas, f, ensure_ascii=False)


def generar_escape_1mb(carpeta):
    s = cadena_1mb()
    escribir(os.path.join(carpeta, "escape_1mb.lg"),
             "import html\nprint(ui_escapar(%s))\nprint(\"%s\")\n" % (lit(s), SENTINEL))
    with open(os.path.join(carpeta, "escape_1mb.json"), "w", encoding="utf-8") as f:
        json.dump(s, f, ensure_ascii=False)


CONTADOR = itertools.count(1)  # global: los nombres de variable son únicos en todo el programa


class Generador:
    """Emite líneas lg que construyen un árbol y devuelve su especificación."""

    def __init__(self, rnd):
        self.rnd = rnd
        self.lineas = []

    def nombre(self):
        return "v%d" % next(CONTADOR)

    def atributos(self, attrs):
        # html.lg: atributos como lista plana alternando nombre y valor.
        var = self.nombre()
        plana = ", ".join(lit(x) for kv in attrs.items() for x in kv)
        self.lineas.append("%s: List[str] = [%s]" % (var, plana))
        return var

    def nodo(self, prof):
        r = self.rnd.random()
        if prof == 0 or r < 0.35:
            if self.rnd.random() < 0.7:
                s = cadena_aleatoria(self.rnd, 12)
                v = self.nombre()
                self.lineas.append("%s = ui_texto(%s)" % (v, lit(s)))
                return v, {"t": s}
            t = cadena_aleatoria(self.rnd, 8)
            acc = self.rnd.randint(-5, 99)
            v = self.nombre()
            self.lineas.append("%s = ui_boton(%s, %d)" % (v, lit(t), acc))
            return v, {"tag": "button", "attrs": {}, "children": [{"t": t}]}
        # Elemento con 1..3 hijos (nunca vacío: el literal [] sin tipo no es fiable en generados).
        attrs = {}
        for k in self.rnd.sample(CLAVES, self.rnd.randint(0, 2)):
            attrs[k] = self.rnd.choice(VALORES_HOSTILES + [cadena_aleatoria(self.rnd, 6)])
        va = self.atributos(attrs)
        hijos = [self.nodo(prof - 1) for _ in range(self.rnd.randint(1, 3))]
        v = self.nombre()
        etiqueta = self.rnd.choice(ETIQUETAS)
        self.lineas.append("%s = ui_elemento(%s, %s, [%s])" % (
            v, lit(etiqueta), va, ", ".join(h[0] for h in hijos)))
        return v, {"tag": etiqueta, "attrs": attrs, "children": [h[1] for h in hijos]}


def nodo_fijo(lineas, nombre_attrs, etiqueta, attrs, hijos_vars):
    lineas.append("%s = ui_elemento(%s, %s, [%s])" % (
        nombre_attrs[1], lit(etiqueta), nombre_attrs[0], ", ".join(hijos_vars)))


def generar_arboles(carpeta, semilla, n_aleatorios):
    rnd = random.Random(semilla + 1)
    lineas = ["import render"]
    specs = []

    def cerrar(nombre_raiz, spec):
        lineas.append("print(ui_render(%s))" % nombre_raiz)
        lineas.append('print("%s")' % SENTINEL)
        specs.append(spec)

    # Aleatorios.
    for _ in range(n_aleatorios):
        g = Generador(rnd)
        raiz, spec = g.nodo(rnd.randint(1, 4))
        lineas.extend(g.lineas)
        cerrar(raiz, spec)

    # Casos hostiles fijos.
    def fijo(attrs, etiqueta, hijos_fns):
        g = Generador(rnd)
        va = g.atributos(attrs)
        hijos = [fn(g) for fn in hijos_fns]
        v = g.nombre()
        g.lineas.append("%s = ui_elemento(%s, %s, [%s])" % (
            v, lit(etiqueta), va, ", ".join(h[0] for h in hijos)))
        return g.lineas, v, {"tag": etiqueta, "attrs": attrs, "children": [h[1] for h in hijos]}

    def txt(s):
        return lambda g: _texto(g, s)

    def _texto(g, s):
        v = g.nombre()
        g.lineas.append("%s = ui_texto(%s)" % (v, lit(s)))
        return v, {"t": s}

    hostiles = [
        ({"title": 'x" onclick="alert(1)'}, "a", [txt("ok")]),
        ({"title": "'><script>alert(1)</script>"}, "span", [txt("<script>alert(1)</script>")]),
        ({}, "p", [txt("<img src=x onerror=alert(1)>")]),
        ({}, "p", [txt("&amp;")]),
        ({"href": 'javascript:alert(1)"'}, "a", [txt("enlace")]),
        ({}, "div", []),  # nodo con cero hijos
        ({}, "div", [txt("")]),
    ]
    for attrs, et, fns in hostiles:
        l, v, spec = fijo(attrs, et, fns)
        lineas.extend(l)
        cerrar(v, spec)

    # Boton con texto hostil.
    g = Generador(rnd)
    vb = g.nombre()
    g.lineas.append('%s = ui_boton("</button><script>alert(1)</script>", -1)' % vb)
    lineas.extend(g.lineas)
    cerrar(vb, {"tag": "button", "attrs": {}, "children": [{"t": "</button><script>alert(1)</script>"}]})

    # Profundidad 1000 (cadena lineal, sin recursión en lg).
    g = Generador(rnd)
    va = g.atributos({})
    lineas.extend(g.lineas)
    lineas.append('mut prof = ui_texto("hoja")')
    spec_prof = {"t": "hoja"}
    for _ in range(1000):
        spec_prof = {"tag": "div", "attrs": {}, "children": [spec_prof]}
        lineas.append("prof = ui_elemento(\"div\", %s, [prof])" % va)
    cerrar("prof", spec_prof)

    escribir(os.path.join(carpeta, "arboles.lg"), "\n".join(lineas) + "\n")
    with open(os.path.join(carpeta, "arboles.json"), "w", encoding="utf-8") as f:
        json.dump(specs, f, ensure_ascii=False)


def generar(carpeta, semilla, n):
    os.makedirs(carpeta, exist_ok=True)
    generar_escape_fuzz(carpeta, semilla, n)
    generar_escape_1mb(carpeta)
    generar_arboles(carpeta, semilla, 500)
    return 0


# ---------------------------------------------------------------- comprobación del escape

RE_NUM = re.compile(r"&#(x[0-9a-fA-F]+|[0-9]+);")


def normalizar_comilla(s):
    return s.replace("&#39;", "&#x27;").replace("&apos;", "&#x27;")


def decodificar_no_ascii(s):
    def rep(m):
        cp = int(m.group(1)[1:], 16) if m.group(1).startswith("x") else int(m.group(1))
        return chr(cp) if cp > 127 else m.group(0)
    return RE_NUM.sub(rep, s)


def clasificar(salida, entrada):
    esperado = html.escape(entrada, quote=True)
    if salida == esperado:
        return "EXACTO"
    if normalizar_comilla(salida) == esperado:
        return "EQUIV_COMILLA"
    if decodificar_no_ascii(normalizar_comilla(salida)) == esperado:
        return "DIF_UTF8"
    return "FAIL"


def leer_salida(ruta):
    with open(ruta, "rb") as f:
        return f.read().decode("utf-8", errors="surrogateescape")


def registros(texto):
    partes = texto.split(SENTINEL + "\n")
    if partes and partes[-1] == "":
        partes.pop()
    return [p[:-1] if p.endswith("\n") else p for p in partes]


def comprobar_escape(carpeta, ruta_salida, nombre_json, etiqueta, nombre_lg):
    with open(os.path.join(carpeta, nombre_json), "r", encoding="utf-8") as f:
        entradas = json.load(f)
    if isinstance(entradas, str):
        entradas = [entradas]
    recs = registros(leer_salida(ruta_salida))
    conteo = {"EXACTO": 0, "EQUIV_COMILLA": 0, "DIF_UTF8": 0, "FAIL": 0}
    fallos = []
    if len(recs) != len(entradas):
        print("%s: se esperaban %d registros y hay %d" % (etiqueta, len(entradas), len(recs)))
        conteo["FAIL"] += 1
    for i, (salida, entrada) in enumerate(zip(recs, entradas)):
        c = clasificar(salida, entrada)
        conteo[c] += 1
        if c == "FAIL" and len(fallos) < 3:
            fallos.append((i, entrada, salida))
    total = len(entradas)
    print("%s (%s): %d entradas" % (etiqueta, nombre_lg, total))
    print("  EXACTO=%d EQUIV_COMILLA=%d DIF_UTF8=%d FAIL=%d" % (
        conteo["EXACTO"], conteo["EQUIV_COMILLA"], conteo["DIF_UTF8"], conteo["FAIL"]))
    for i, entrada, salida in fallos:
        print("  FAIL #%d entrada=%r" % (i, entrada[:120]))
        print("        salida=%r" % salida[:200])
    return conteo["FAIL"] == 0 and len(recs) == total


# ---------------------------------------------------------------- comprobación de árboles

URL_ATTRS = {"href", "src", "action", "formaction", "poster", "xlink:href"}
VACIAS = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "source", "track", "wbr"}
ALLOWED_TAGS = set(ETIQUETAS) | {"button"}
PROHIBIDAS = {"script", "style", "iframe", "svg", "math", "object", "embed", "form", "input", "img",
              "link", "meta", "base", "frame", "html", "head", "body", "template"}


class Arbol(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.raiz = {"tag": "#raiz", "attrs": {}, "children": []}
        self.pila = [self.raiz]
        self.errores = []
        self.urls_js = []

    def handle_starttag(self, tag, attrs):
        if tag in VACIAS:
            self.errores.append("etiqueta vacía no esperada <%s>" % tag)
        if tag in PROHIBIDAS:
            self.errores.append("etiqueta prohibida <%s>" % tag)
        if tag not in ALLOWED_TAGS:
            self.errores.append("etiqueta no generada <%s>" % tag)
        for k, v in attrs:
            v = v or ""
            if k.startswith("on") and not (tag == "button" and v.strip().lstrip("-").isdigit()):
                self.errores.append("atributo peligroso %s=%r en <%s>" % (k, v[:40], tag))
            if k in URL_ATTRS and v.strip().lower().startswith("javascript:"):
                self.urls_js.append("<%s %s>" % (tag, k))
        nodo = {"tag": tag, "attrs": {k: (v or "") for k, v in attrs}, "children": []}
        self.pila[-1]["children"].append(nodo)
        if tag in VACIAS:
            return
        self.pila.append(nodo)

    def handle_startendtag(self, tag, attrs):
        self.handle_starttag(tag, attrs)
        if tag not in VACIAS and len(self.pila) > 1:
            self.pila.pop()

    def handle_endtag(self, tag):
        if len(self.pila) == 1:
            self.errores.append("cierre sin apertura </%s>" % tag)
            return
        if self.pila[-1]["tag"] != tag:
            self.errores.append("anidamiento incorrecto: se esperaba </%s>, llega </%s>" % (
                self.pila[-1]["tag"], tag))
            # Recuperación: desapilar hasta encontrar la apertura correspondiente, si existe.
            for i in range(len(self.pila) - 1, 0, -1):
                if self.pila[i]["tag"] == tag:
                    del self.pila[i:]
                    return
            return
        self.pila.pop()

    def handle_data(self, data):
        hijos = self.pila[-1]["children"]
        if hijos and isinstance(hijos[-1], str):
            hijos[-1] += data
        else:
            hijos.append(data)


def _fusionar(hijos):
    """Une textos contiguos y descarta los tramos vacíos o solo de espacios (tras fusionar)."""
    fusion = []
    for h in hijos:
        if isinstance(h, str):
            if fusion and isinstance(fusion[-1], str):
                fusion[-1] += h
            else:
                fusion.append(h)
        else:
            fusion.append(h)
    return [h for h in fusion if not (isinstance(h, str) and h.strip() == "")]


def hijos_utiles_html(hijos):
    return _fusionar(hijos)


def hijos_utiles_spec(hijos):
    return _fusionar([h["t"] if "t" in h else h for h in hijos])


def comparar(spec, real, ruta, dif):
    if isinstance(spec, dict) and "t" in spec:
        spec = spec["t"]
    if isinstance(spec, str):
        if not isinstance(real, str) or real.strip() != spec.strip():
            dif.append("%s: texto esperado %r, obtenido %r" % (ruta, spec[:60], str(real)[:60]))
        return
    if isinstance(real, str) or real["tag"] != spec["tag"]:
        dif.append("%s: se esperaba <%s>, obtenido %r" % (
            ruta, spec["tag"], real if isinstance(real, str) else real["tag"]))
        return
    for k, v in spec["attrs"].items():
        v = valor_esperado_attr(k, v)  # el render sustituye URLs peligrosas por "#" (ui_url_peligrosa)
        if real["attrs"].get(k) != v:
            dif.append("%s: atributo %s esperado %r, obtenido %r" % (
                ruta, k, v[:60], str(real["attrs"].get(k))[:60]))
    hs = hijos_utiles_spec(spec["children"])
    hr = hijos_utiles_html(real["children"])
    if len(hs) != len(hr):
        dif.append("%s: %d hijos esperados, %d obtenidos" % (ruta, len(hs), len(hr)))
        return
    for i, (a, b) in enumerate(zip(hs, hr)):
        comparar(a, b, "%s/%s[%d]" % (ruta, spec["tag"], i), dif)


def profundidad_spec(nodo):
    prof = 0
    cola = [(nodo, 0)]
    while cola:
        n, d = cola.pop()
        if "t" in n:
            continue
        prof = max(prof, d + 1)
        for h in n["children"]:
            cola.append((h, d + 1))
    return prof



def valor_esperado_attr(nombre, valor):
    """Réplica de ui_atributo_url + ui_url_peligrosa de lib/ui/render.lg: una URL con esquema
    javascript:, vbscript: o data: (tras quitar todo salvo letras, dígitos y ':') se muestra como '#'."""
    if nombre not in URL_ATTRS:
        return valor
    limpia = "".join(c for c in valor.lower() if c.isascii() and (c.isalnum() or c == ":"))
    if limpia.startswith(("javascript:", "vbscript:", "data:")):
        return "#"
    return valor

def comprobar_arboles(carpeta, ruta_salida):
    with open(os.path.join(carpeta, "arboles.json"), "r", encoding="utf-8") as f:
        specs = json.load(f)
    recs = registros(leer_salida(ruta_salida))
    fallos = 0
    if len(recs) != len(specs):
        print("arboles: se esperaban %d registros y hay %d" % (len(specs), len(recs)))
        return False
    detalles = []
    urls_js_total = 0
    for i, (rec, spec) in enumerate(zip(recs, specs)):
        p = Arbol()
        try:
            p.feed(rec)
            p.close()
        except Exception as e:  # el parser no debe lanzar; si lo hace, es un fallo
            p.errores.append("excepción del parser: %s" % e)
        dif = list(p.errores)
        urls_js_total += len(p.urls_js)
        if len(p.pila) != 1:
            dif.append("etiquetas sin cerrar: %s" % [n["tag"] for n in p.pila[1:4]])
        cuerpo = hijos_utiles_html(p.raiz["children"])
        esperado = hijos_utiles_spec([spec])
        if len(cuerpo) != len(esperado):
            dif.append("raíz: %d nodos esperados, %d obtenidos" % (len(esperado), len(cuerpo)))
        elif cuerpo:
            comparar(spec, cuerpo[0], "", dif)
        if dif:
            fallos += 1
            if len(detalles) < 3:
                detalles.append((i, dif[:3], rec[:160]))
    prof_esperada = max(profundidad_spec(s) for s in specs) if specs else 0
    print("  nota: %d atributos de URL con javascript: (se comprueban en BUG_href_javascript, no aquí)" % urls_js_total)
    print("arboles (arboles.lg): %d árboles, %d con fallos (cadena más profunda: %d niveles)" % (
        len(specs), fallos, prof_esperada))
    for i, dif, rec in detalles:
        print("  FAIL árbol #%d: %s" % (i, "; ".join(dif)))
        print("        render=%r" % rec)
    return fallos == 0


def comprobar(carpeta, salida_fuzz, salida_1mb, salida_arboles):
    ok1 = comprobar_escape(carpeta, salida_fuzz, "escape_fuzz.json", "escape fuzz", "escape_fuzz.lg")
    ok2 = comprobar_escape(carpeta, salida_1mb, "escape_1mb.json", "escape 1 MB", "escape_1mb.lg")
    ok3 = comprobar_arboles(carpeta, salida_arboles)
    return 0 if (ok1 and ok2 and ok3) else 1


def main(argv):
    if len(argv) >= 3 and argv[1] == "generar":
        semilla = int(argv[3]) if len(argv) > 3 else 20260
        n = int(argv[4]) if len(argv) > 4 else 3000
        return generar(argv[2], semilla, n)
    if len(argv) == 6 and argv[1] == "comprobar":
        return comprobar(argv[2], argv[3], argv[4], argv[5])
    print("uso: fuzz_escape.py generar <carpeta> [semilla] [cadenas]", file=sys.stderr)
    print("     fuzz_escape.py comprobar <carpeta> <salida_fuzz> <salida_1mb> <salida_arboles>", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.setrecursionlimit(20000)
    sys.exit(main(sys.argv))
