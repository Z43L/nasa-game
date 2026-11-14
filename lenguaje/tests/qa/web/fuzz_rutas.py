#!/usr/bin/env python3
"""Fuzz de enrutamiento: genera un programa .lg y su salida esperada según una referencia en Python.

Uso: fuzz_rutas.py --seed S --n N --salida DIR
Escribe DIR/rutas.lg (20 rutas con web_agregar y N consultas con web_enrutar, una línea de salida
por consulta) y DIR/rutas.expected (id de la ruta encontrada, o "-" si no hay coincidencia).

Semántica de referencia, la documentada en lib/web/router.lg:
  - La query string (desde "?") se quita de la ruta antes de comparar.
  - Patrón y ruta se parten por "/" descartando los segmentos vacíos.
  - Segmento literal: igualdad exacta. Segmento ":nombre" (más de un carácter): cualquier
    segmento. Un segmento ":" solo es literal.
  - El número de segmentos debe coincidir exactamente.
  - Método: igualdad exacta, sensible a mayúsculas.
  - Gana la primera ruta registrada que coincida (orden de web_agregar).
"""
import argparse
import os
import random

METODOS = ["GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "get", "FOO"]
VOCAB = ["usuarios", "pedidos", "api", "v1", "id", "x", "y", "items", "ver"]
NUM_RUTAS = 20


def patron_aleatorio(rng):
    segs = []
    for _ in range(rng.randint(1, 4)):
        if rng.random() < 0.3:
            segs.append(":" + rng.choice(VOCAB))
        else:
            segs.append(rng.choice(VOCAB))
    return "/" + "/".join(segs)


def generar_rutas(rng):
    rutas = []
    vistas = set()
    while len(rutas) < NUM_RUTAS:
        par = (rng.choice(METODOS[:6]), patron_aleatorio(rng))
        if par not in vistas:
            vistas.add(par)
            rutas.append(par)
    return rutas


def partir(s):
    return [x for x in s.split("/") if x != ""]


def coincide(patron, metodo_r, metodo, ruta):
    if metodo_r != metodo:
        return False
    ruta = ruta.split("?", 1)[0]
    ps = partir(patron)
    rs = partir(ruta)
    if len(ps) != len(rs):
        return False
    for p, r in zip(ps, rs):
        es_captura = len(p) > 1 and p.startswith(":")
        if not es_captura and p != r:
            return False
    return True


def referencia(rutas, metodo, ruta):
    for idx, (metodo_r, patron) in enumerate(rutas):
        if coincide(patron, metodo_r, metodo, ruta):
            return str(idx + 1)
    return "-"


def ruta_aleatoria(rng, rutas):
    ruta = ruta_base(rng, rutas)
    if rng.random() < 0.15:
        ruta += rng.choice(["?", "?q=1", "?a=b/c", "?:x"])
    return ruta


def ruta_base(rng, rutas):
    k = rng.random()
    if k < 0.4:
        # instancia de un patrón, con parámetros vacíos o inválidos de vez en cuando
        patron = rng.choice(rutas)[1]
        salida = []
        for seg in patron.split("/")[1:]:
            if seg.startswith(":"):
                salida.append(rng.choice(VOCAB + ["", "zz9"]))
            elif rng.random() < 0.9:
                salida.append(seg)
            else:
                salida.append(rng.choice(VOCAB))
        return "/" + "/".join(salida)
    if k < 0.7:
        return "/" + "/".join(rng.choice(VOCAB + ["", "a"]) for _ in range(rng.randint(0, 6)))
    return "/" + "".join(rng.choice("abcxyz0123") for _ in range(rng.randint(1, 12)))


def main():
    p = argparse.ArgumentParser(description="Genera el fuzz de enrutamiento y su salida esperada.")
    p.add_argument("--seed", type=int, default=4242)
    p.add_argument("--n", type=int, default=5000)
    p.add_argument("--salida", required=True)
    a = p.parse_args()
    rng = random.Random(a.seed)
    rutas = generar_rutas(rng)
    consultas = []
    for _ in range(a.n):
        metodo = rng.choice(METODOS)
        ruta = ruta_aleatoria(rng, rutas)
        consultas.append((metodo, ruta))

    lineas = ["import app", "import router", "", "e = web_nuevo_enrutador()"]
    for idx, (metodo, patron) in enumerate(rutas):
        lineas.append(f'web_agregar(e, "{metodo}", "{patron}", {idx + 1})')
    for i, (metodo, ruta) in enumerate(consultas):
        lineas.append(f'r{i} = web_enrutar(e, "{metodo}", "{ruta}")')
        lineas.append(f'if r{i}.encontrada:')
        lineas.append(f'    print(str(r{i}.id))')
        lineas.append('else:')
        lineas.append('    print("-")')
    esperado = [referencia(rutas, m, r) for m, r in consultas]

    os.makedirs(a.salida, exist_ok=True)
    with open(os.path.join(a.salida, "rutas.lg"), "w", encoding="utf-8") as f:
        f.write("\n".join(lineas) + "\n")
    with open(os.path.join(a.salida, "rutas.expected"), "w", encoding="utf-8") as f:
        f.write("\n".join(esperado) + "\n")
    encontradas = sum(1 for x in esperado if x != "-")
    print(f"METRICA fuzz_rutas consultas={a.n} encontradas={encontradas} no_encontradas={a.n - encontradas}")


if __name__ == "__main__":
    main()
