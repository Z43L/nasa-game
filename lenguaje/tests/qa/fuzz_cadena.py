#!/usr/bin/env python3
# Fuzzing de stdlib/cadena: genera un programa lg con llamadas aleatorias a buscar y recortar,
# y su salida esperada calculada con una referencia en Python (str.find y recorte con límites).
# Uso: fuzz_cadena.py <carpeta_destino> [semilla] [iteraciones]
# Escribe cadena_fuzz.lg y cadena_fuzz.expected en la carpeta destino.
import os
import random
import sys


def ref_buscar(s, aguja):
    return s.find(aguja)


def ref_recortar(s, ini, fin):
    ini = max(ini, 0)
    fin = min(fin, len(s))
    return "" if ini >= fin else s[ini:fin]


def main():
    destino = sys.argv[1]
    semilla = int(sys.argv[2]) if len(sys.argv) > 2 else 20260
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 300
    rnd = random.Random(semilla)

    def texto(maxlen):
        return "".join(rnd.choice("ab ") for _ in range(rnd.randint(0, maxlen)))

    lineas_lg = ["import cadena"]
    esperado = []
    for _ in range(n):
        s = texto(8)
        if rnd.random() < 0.5:
            aguja = texto(4)
            lineas_lg.append('print(buscar("%s", "%s"))' % (s, aguja))
            esperado.append(str(ref_buscar(s, aguja)))
        else:
            ini = rnd.randint(-12, 12)
            fin = rnd.randint(-12, 12)
            lineas_lg.append('print("[" + recortar("%s", %d, %d) + "]")' % (s, ini, fin))
            esperado.append("[" + ref_recortar(s, ini, fin) + "]")

    os.makedirs(destino, exist_ok=True)
    with open(os.path.join(destino, "cadena_fuzz.lg"), "w", encoding="utf-8") as f:
        f.write("\n".join(lineas_lg) + "\n")
    with open(os.path.join(destino, "cadena_fuzz.expected"), "w", encoding="utf-8") as f:
        f.write("\n".join(esperado) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
