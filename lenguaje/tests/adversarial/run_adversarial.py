#!/usr/bin/env python3
"""Ejecuta las pruebas adversariales de tests/adversarial/{parser,semantica}.

Uso: run_adversarial.py <ruta_lgc>

Cada caso es un par NOMBRE.lg / NOMBRE.expect (los .lg sin .expect, como los módulos de
mods/, se ignoran). Formato de la primera línea del .expect:

  OK                    compila, ejecuta con código 0 (la salida no se comprueba)
  OK <texto>            igual, y la salida estándar debe ser exactamente <texto> + lo que
                        siga en el archivo (líneas siguientes incluidas)
  ERROR                 falla al compilar o al ejecutar (código != 0)
  ERROR_COMPILE [alt]   debe fallar al compilar; si hay alternativas, el mensaje debe
                        contener alguna (sin distinguir mayúsculas)
  ERROR_RUN [alt]       debe compilar y fallar al ejecutar; igual que arriba con alternativas

Las alternativas van separadas por '|||'. Un segmento que empieza por ERROR, ERROR_COMPILE o
ERROR_RUN abre un nuevo caso (por ejemplo: 'ERROR_RUN cero ||| ERROR_COMPILE cero'); el caso
pasa si cumple cualquiera de ellos.

Sale con código 1 si algún caso falla.
"""
import concurrent.futures
import os
import subprocess
import sys
import tempfile

COMPILE_TIMEOUT = 120  # segundos
RUN_TIMEOUT = 60
KINDS = ("ERROR_COMPILE", "ERROR_RUN", "ERROR")


def parse_expect(text):
    """Devuelve ('OK', expected_stdout_or_None) o ('ERROR', [(kind, [alts...]), ...])."""
    first, _, _ = text.partition("\n")
    if first == "OK" or first.startswith("OK "):
        if first == "OK":
            return ("OK", None)
        return ("OK", text[3:])
    if not first.startswith("ERROR"):
        raise ValueError("primera línea no reconocida: %r" % first)
    cases = []
    current = None
    for seg in first.split("|||"):
        seg = seg.strip()
        if not seg:
            continue
        head, _, rest = seg.partition(" ")
        if head in KINDS:
            current = (head, [])
            cases.append(current)
            seg = rest.strip()
            if not seg:
                continue
        if current is None:
            raise ValueError("alternativa sin tipo de error: %r" % seg)
        current[1].append(seg.lower())
    if not cases:
        cases.append(("ERROR", []))
    return ("ERROR", cases)


def run(cmd, cwd, timeout):
    try:
        p = subprocess.run(cmd, cwd=cwd, capture_output=True, timeout=timeout)
    except subprocess.TimeoutExpired:
        return None
    return p


def check_case(lgc, src, tmpdir):
    here = os.path.dirname(src)
    name = os.path.splitext(os.path.basename(src))[0]
    expect_path = os.path.splitext(src)[0] + ".expect"
    with open(expect_path, encoding="utf-8", errors="replace") as fh:
        expect_text = fh.read()
    try:
        kind, detail = parse_expect(expect_text)
    except ValueError as e:
        return False, "expect mal formado: %s" % e

    binp = os.path.join(tmpdir, name)
    c = run([lgc, src, "-o", binp], here, COMPILE_TIMEOUT)
    if c is None:
        return False, "tiempo agotado al compilar"
    c_out = (c.stdout + c.stderr).decode("utf-8", errors="replace").lower()
    compiled = c.returncode == 0

    if kind == "OK":
        if not compiled:
            return False, "fallo al compilar: %s" % first_line(c_out)
        r = run([binp], tmpdir, RUN_TIMEOUT)
        if r is None:
            return False, "tiempo agotado al ejecutar"
        if r.returncode != 0:
            return False, "terminó con código %d: %s" % (r.returncode, first_line(
                (r.stdout + r.stderr).decode("utf-8", errors="replace").lower()))
        if detail is not None:
            got = r.stdout.decode("utf-8", errors="replace")
            if got != detail:
                return False, "salida distinta: esperada %r, obtenida %r" % (detail[:120], got[:120])
        return True, ""

    # Casos de error: detail es lista de (tipo, alternativas).
    reasons = []
    for case_kind, alts in detail:
        if case_kind in ("ERROR", "ERROR_COMPILE") and not compiled:
            if not alts or any(a in c_out for a in alts):
                return True, ""
            reasons.append("mensaje de compilación no contiene %s" % alts)
            continue
        if case_kind in ("ERROR", "ERROR_RUN") and compiled:
            r = run([binp], tmpdir, RUN_TIMEOUT)
            if r is None:
                reasons.append("tiempo agotado al ejecutar")
                continue
            if r.returncode == 0:
                reasons.append("ejecución terminó con código 0")
                continue
            if not alts:
                return True, ""
            r_out = (r.stdout + r.stderr).decode("utf-8", errors="replace").lower()
            if any(a in r_out for a in alts):
                return True, ""
            reasons.append("mensaje de ejecución no contiene %s: %s" % (alts, first_line(r_out)))
            continue
        if case_kind == "ERROR_COMPILE" and compiled:
            reasons.append("compiló y se esperaba error de compilación")
        elif case_kind == "ERROR_RUN" and not compiled:
            reasons.append("fallo al compilar y se esperaba error de ejecución: %s" % first_line(c_out))
    return False, "; ".join(reasons) or "ningún caso cumplido"


def first_line(s):
    s = s.strip()
    return s.split("\n", 1)[0][:200] if s else "(sin salida)"


def main(argv):
    if len(argv) < 2:
        print("uso: %s <ruta_lgc>" % argv[0], file=sys.stderr)
        return 2
    lgc = os.path.abspath(argv[1])
    if not os.access(lgc, os.X_OK):
        print("no existe el ejecutable lgc: %s" % lgc, file=sys.stderr)
        return 2

    root = os.path.dirname(os.path.abspath(__file__))
    cases = []
    for dirpath, _dirs, files in os.walk(root):
        for f in files:
            if f.endswith(".lg") and os.path.exists(os.path.join(dirpath, f[:-3] + ".expect")):
                cases.append(os.path.join(dirpath, f))
    cases.sort()

    passed = 0
    failed = []
    with tempfile.TemporaryDirectory() as tmp:
        with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 2) as pool:
            futures = {}
            for src in cases:
                sub = tempfile.mkdtemp(dir=tmp)
                futures[pool.submit(check_case, lgc, src, sub)] = src
            results = {}
            for fut in concurrent.futures.as_completed(futures):
                src = futures[fut]
                try:
                    ok, why = fut.result()
                except Exception as e:  # un fallo del propio runner no debe pasar en silencio
                    ok, why = False, "excepción del runner: %r" % e
                results[src] = (ok, why)
    for src in cases:
        ok, why = results[src]
        rel = os.path.relpath(src, root)
        if ok:
            passed += 1
            print("PASS  %s" % rel)
        else:
            failed.append(rel)
            print("FAIL  %s: %s" % (rel, why))

    print()
    print("Resumen adversarial: %d PASS, %d FAIL" % (passed, len(failed)))
    if failed:
        for rel in failed:
            print("  - %s" % rel)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
