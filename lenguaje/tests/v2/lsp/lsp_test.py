#!/usr/bin/env python3
"""Prueba de protocolo del servidor lglsp.

Uso: python3 tests/v2/lsp/lsp_test.py ruta/a/lglsp

Lanza el servidor, le envía mensajes JSON-RPC con cabecera Content-Length por stdin y
comprueba las respuestas y notificaciones por stdout. Sale con código 0 si todo pasa.
"""
import json
import os
import queue
import subprocess
import sys
import threading

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
DOC_URI = "file://" + ROOT + "/tests/v2/lsp/fixtures/demo.lg"
CLS_URI = "file://" + ROOT + "/tests/v2/lsp/fixtures/punto.lg"

# Línea 5 (1-based): reasignación de `x` que no es mut -> error de inmutabilidad.
DEMO_TEXT = (
    "fn suma(a: int, b: int) -> int:\n"   # 1
    "    return a + b\n"                  # 2
    "\n"                                  # 3
    "x = 1\n"                             # 4
    "x = 2\n"                             # 5  <- error esperado en la línea 5 (0-based 4)
    "print(suma(x, 3))\n"                 # 6
)

PUNTO_TEXT = (
    "struct Punto:\n"                     # 0-based 0
    "    x: float\n"                      # 1
    "    y: float\n"                      # 2
    "\n"                                  # 3
    "impl Punto:\n"                       # 4
    "    fn norma(self) -> float:\n"      # 5
    "        return self.x * self.x + self.y * self.y\n"  # 6
    "\n"                                  # 7
    "p = Punto(x: 1.0, y: 2.0)\n"         # 8
    "print(p.norma())\n"                  # 9
)


class Client:
    def __init__(self, binary):
        self.proc = subprocess.Popen(
            [binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        self.inbox = queue.Queue()
        self.backlog = []
        self.reader = threading.Thread(target=self._read_loop, daemon=True)
        self.reader.start()

    def _read_loop(self):
        out = self.proc.stdout
        while True:
            length = None
            while True:
                line = out.readline()
                if not line:
                    self.inbox.put(None)
                    return
                line = line.strip()
                if not line:
                    break
                if line.lower().startswith(b"content-length:"):
                    length = int(line.split(b":", 1)[1])
            if length is None:
                self.inbox.put(("bad", b""))
                continue
            body = out.read(length)
            try:
                self.inbox.put(("ok", json.loads(body.decode("utf-8"))))
            except ValueError:
                self.inbox.put(("bad", body))

    def send_raw(self, data: bytes):
        self.proc.stdin.write(data)
        self.proc.stdin.flush()

    def send(self, obj):
        body = json.dumps(obj).encode("utf-8")
        self.send_raw(b"Content-Length: " + str(len(body)).encode() + b"\r\n\r\n" + body)

    def wait(self, pred, timeout=10.0):
        """Devuelve el primer mensaje que cumple pred; los demás se guardan en backlog."""
        for i, m in enumerate(self.backlog):
            if pred(m):
                return self.backlog.pop(i)
        while True:
            try:
                item = self.inbox.get(timeout=timeout)
            except queue.Empty:
                return None
            if item is None:
                return None
            kind, msg = item
            if kind != "ok":
                continue
            if pred(msg):
                return msg
            self.backlog.append(msg)

    def request(self, rid, method, params):
        self.send({"jsonrpc": "2.0", "id": rid, "method": method, "params": params})
        return self.wait(lambda m: m.get("id") == rid)


def pos(line, character):
    return {"line": line, "character": character}


def main():
    binary = sys.argv[1]
    results = []

    def check(name, ok, detail=""):
        results.append((name, bool(ok)))
        print(("PASS " if ok else "FAIL ") + name + (("  -- " + detail) if detail and not ok else ""))

    c = Client(binary)

    # 1. initialize
    r = c.request(1, "initialize", {"processId": None, "rootUri": None, "capabilities": {}})
    caps = (r or {}).get("result", {}).get("capabilities", {})
    check("initialize devuelve capacidades", caps.get("definitionProvider") is True
          and caps.get("hoverProvider") is True and "completionProvider" in caps,
          json.dumps(r))
    c.send({"jsonrpc": "2.0", "method": "initialized", "params": {}})

    # 2. didOpen con error de inmutabilidad -> publishDiagnostics en la línea 5 (0-based 4)
    c.send({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {
        "uri": DOC_URI, "languageId": "lg", "version": 1, "text": DEMO_TEXT}}})
    c.send({"jsonrpc": "2.0", "method": "textDocument/didSave", "params": {"textDocument": {"uri": DOC_URI}}})
    pub = c.wait(lambda m: m.get("method") == "textDocument/publishDiagnostics"
                 and m.get("params", {}).get("uri") == DOC_URI
                 and len(m["params"].get("diagnostics", [])) > 0)
    diags = (pub or {}).get("params", {}).get("diagnostics", [])
    check("didSave publica el error de inmutabilidad", len(diags) == 1, json.dumps(pub))
    if diags:
        d = diags[0]
        check("diagnóstico en línea 5 (0-based 4)", d["range"]["start"]["line"] == 4,
              json.dumps(d))
        check("diagnóstico en columna 1 (0-based 0)", d["range"]["start"]["character"] == 0,
              json.dumps(d))
        check("severidad error (1)", d.get("severity") == 1, json.dumps(d))
        check("mensaje menciona inmutable", "inmutable" in d.get("message", "").lower(), d.get("message"))

    # 3. definition: llamada a la función `suma` en la línea 6 -> definición en línea 1, col 3
    r = c.request(2, "textDocument/definition", {"textDocument": {"uri": DOC_URI},
                                                 "position": pos(5, 6)})
    loc = (r or {}).get("result")
    ok = isinstance(loc, list) and len(loc) == 1 and loc[0]["uri"] == DOC_URI \
        and loc[0]["range"]["start"] == pos(0, 3) and loc[0]["range"]["end"] == pos(0, 7)
    check("definition de función suma -> línea 1, col 4 (0-based 0,3)", ok, json.dumps(r))

    # 4. definition de variable `x` usada en la llamada -> declaración `x = 1` (línea 4)
    r = c.request(3, "textDocument/definition", {"textDocument": {"uri": DOC_URI},
                                                 "position": pos(5, 11)})
    loc = (r or {}).get("result") or [{}]
    check("definition de variable x -> línea 4", loc[0].get("range", {}).get("start") == pos(3, 0),
          json.dumps(r))

    # 5. definition de parámetro `a` en `return a + b` -> parámetro en la firma
    r = c.request(4, "textDocument/definition", {"textDocument": {"uri": DOC_URI},
                                                 "position": pos(1, 11)})
    loc = (r or {}).get("result") or [{}]
    check("definition de parámetro a -> firma de suma", loc[0].get("range", {}).get("start") == pos(0, 8),
          json.dumps(r))

    # 6. hover sobre suma
    r = c.request(5, "textDocument/hover", {"textDocument": {"uri": DOC_URI}, "position": pos(5, 6)})
    value = (((r or {}).get("result") or {}).get("contents") or {}).get("value", "")
    check("hover de suma muestra la firma", "fn suma(a: int, b: int) -> int" in value, json.dumps(r))

    # 7. completion en ámbito general incluye palabras reservadas, builtins y nombres del archivo
    r = c.request(6, "textDocument/completion", {"textDocument": {"uri": DOC_URI}, "position": pos(5, 0)})
    labels = {i["label"] for i in ((r or {}).get("result") or {}).get("items", [])}
    check("completion incluye 'fn', 'print', 'suma' y 'x'",
          {"fn", "print", "suma", "x"} <= labels, json.dumps(sorted(labels))[:300])

    # 8. struct y método: definition de método, de campo, de struct; hover; completion tras `p.`
    c.send({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {
        "uri": CLS_URI, "languageId": "lg", "version": 1, "text": PUNTO_TEXT}}})
    r = c.request(7, "textDocument/definition", {"textDocument": {"uri": CLS_URI},
                                                 "position": pos(9, 8)})  # norma en p.norma()
    loc = (r or {}).get("result") or [{}]
    check("definition de método norma -> línea 5, col 8", loc[0].get("range", {}).get("start") == pos(5, 7),
          json.dumps(r))
    r = c.request(8, "textDocument/definition", {"textDocument": {"uri": CLS_URI},
                                                 "position": pos(6, 20)})  # self.x
    loc = (r or {}).get("result") or [{}]
    check("definition de campo self.x -> declaración del campo", loc[0].get("range", {}).get("start") == pos(1, 4),
          json.dumps(r))
    r = c.request(9, "textDocument/definition", {"textDocument": {"uri": CLS_URI},
                                                 "position": pos(8, 5)})  # Punto en p = Punto(...)
    loc = (r or {}).get("result") or [{}]
    check("definition de struct Punto -> línea 1", loc[0].get("range", {}).get("start") == pos(0, 7),
          json.dumps(r))
    r = c.request(10, "textDocument/completion", {"textDocument": {"uri": CLS_URI}, "position": pos(9, 8)})
    labels = {i["label"] for i in ((r or {}).get("result") or {}).get("items", [])}
    check("completion tras 'p.' ofrece campos y métodos", {"x", "y", "norma"} <= labels,
          json.dumps(sorted(labels)))

    # 9. Robustez: JSON mal formado, método desconocido, petición sin método, y el servidor sigue vivo
    c.send_raw(b"Content-Length: 9\r\n\r\n{nope::}}")
    r = c.wait(lambda m: m.get("error", {}).get("code") == -32700)
    check("JSON mal formado -> error -32700 y el servidor sigue", r is not None, "sin respuesta")
    r = c.request(11, "textDocument/noExiste", {})
    check("método desconocido -> error -32601",
          (r or {}).get("error", {}).get("code") == -32601, json.dumps(r))
    c.send({"jsonrpc": "2.0", "id": 12, "params": {}})  # sin 'method'
    r = c.wait(lambda m: m.get("id") == 12)
    check("petición sin método -> error de petición inválida",
          (r or {}).get("error", {}).get("code") == -32600, json.dumps(r))
    r = c.request(13, "textDocument/hover", {"textDocument": {"uri": "file:///no/abierto.lg"},
                                             "position": pos(0, 0)})
    check("hover sobre documento no abierto responde null", r is not None and r.get("result") is None,
          json.dumps(r))

    # 10. shutdown y exit
    r = c.request(14, "shutdown", None)
    check("shutdown responde null", r is not None and "result" in r and r["result"] is None, json.dumps(r))
    c.send({"jsonrpc": "2.0", "method": "exit"})
    try:
        code = c.proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        c.proc.kill()
        code = None
    check("exit tras shutdown termina con código 0", code == 0, "código {}".format(code))

    failed = [n for n, ok in results if not ok]
    print("\n{} comprobaciones, {} fallos".format(len(results), len(failed)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
