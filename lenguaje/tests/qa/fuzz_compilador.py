#!/usr/bin/env python3
# Fuzzing del compilador: muta programas válidos de tests/e2e (truncar, borrar líneas, insertar
# caracteres sueltos, borrar trozos, indentar) y comprueba que lgc:
#   - termina en menos de 20 s (sin cuelgue),
#   - sale con código 0 o 1 (nunca por señal: segfault, abort...),
#   - si sale con 1, escribe algo en stderr.
# Uso: fuzz_compilador.py <ruta_lgc> <carpeta_trabajo> [semilla] [iteraciones]
# Los casos que fallan se conservan en <carpeta_trabajo>/fallos/ y se citan en la salida.
import glob
import os
import random
import shutil
import subprocess
import sys

CARACTERES = "()[]{}\"'\\:,.\t\n #@=+-*/<>!|&0a"


def main():
    lgc = sys.argv[1]
    trabajo = sys.argv[2]
    semilla = int(sys.argv[3]) if len(sys.argv) > 3 else 7
    n = int(sys.argv[4]) if len(sys.argv) > 4 else 300
    rnd = random.Random(semilla)

    aqui = os.path.dirname(os.path.abspath(__file__))
    fuentes = []
    for ruta in sorted(glob.glob(os.path.join(aqui, "..", "e2e", "*.lg"))):
        if os.path.exists(ruta[:-3] + ".expected"):
            with open(ruta, encoding="utf-8") as f:
                fuentes.append(f.read())

    def mutar(texto):
        op = rnd.randrange(5)
        if op == 0:
            return texto[: rnd.randint(0, len(texto))]
        if op == 1:
            lineas = texto.split("\n")
            del lineas[rnd.randrange(len(lineas))]
            return "\n".join(lineas)
        if op == 2:
            p = rnd.randint(0, len(texto))
            return texto[:p] + rnd.choice(CARACTERES) + texto[p:]
        if op == 3:
            p = rnd.randint(0, len(texto))
            q = rnd.randint(p, min(len(texto), p + 6))
            return texto[:p] + texto[q:]
        lineas = texto.split("\n")
        i = rnd.randrange(len(lineas))
        lineas[i] = " " * rnd.randint(1, 8) + lineas[i]
        return "\n".join(lineas)

    fallos = []
    os.makedirs(trabajo, exist_ok=True)
    for i in range(n):
        texto = rnd.choice(fuentes)
        for _ in range(rnd.randint(1, 3)):
            texto = mutar(texto)
        src = os.path.join(trabajo, "fuzz_%04d.lg" % i)
        with open(src, "w", encoding="utf-8") as f:
            f.write(texto)
        salida = os.path.join(trabajo, "fuzz_%04d.bin" % i)
        try:
            r = subprocess.run([lgc, src, "-o", salida], capture_output=True, timeout=20)
        except subprocess.TimeoutExpired:
            fallos.append((src, "cuelgue (más de 20 s)"))
            continue
        motivo = None
        if r.returncode < 0:
            motivo = "señal %d" % -r.returncode
        elif r.returncode not in (0, 1):
            motivo = "código %d" % r.returncode
        elif r.returncode == 1 and not r.stderr.strip():
            motivo = "código 1 sin mensaje en stderr"
        if motivo:
            fallos.append((src, motivo))
            os.makedirs(os.path.join(trabajo, "fallos"), exist_ok=True)
            shutil.copy(src, os.path.join(trabajo, "fallos", os.path.basename(src)))
        if os.path.exists(salida):
            os.remove(salida)

    print("fuzz compilador: %d entradas, %d fallos" % (n, len(fallos)))
    for src, motivo in fallos[:10]:
        print("  FALLO %s: %s" % (src, motivo))
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.exit(main())
