#!/usr/bin/env python3
"""Pruebas de robustez HTTP contra un servidor de prueba (tests/qa/web). Solo biblioteca estándar.

Subcomandos (todos reciben --puerto P en 23000-23999 y --pid PID del servidor):
  garbage  --n N --seed S   N conexiones con basura: binaria, cabeceras de 20 KB, Content-Length
                            incoherente, líneas sin CRLF, método desconocido, resets. El servidor
                            debe seguir vivo y responder 200 a GET /ping.
  timeout  --timeout-ms T   Cliente que conecta y no envía nada: el servidor debe responder o cerrar
                            antes de T + 1000 ms, y atender a otro cliente mientras tanto.
  carga    --n N            N peticiones GET /ping secuenciales: todas 200 y sin fugas de descriptores.
  grande                    Ruta de 1 MB, cabecera de 1 MB y cuerpo de 1 MB: el servidor no debe caer.
  salir                     GET /salir: el servidor debe responder 200 (luego termina solo).

Salida: líneas "METRICA ..." con datos; ante un fallo, una línea "FALLO ..." y código 1.
Código 0 si el subcomando pasa y 2 por uso incorrecto.
"""
import argparse
import random
import socket
import struct
import subprocess
import sys
import time

HOST = "127.0.0.1"
ESPERA = 5.0          # segundos máximos de espera por operación de red del cliente
MAX_RESP = 1 << 20    # respuesta máxima que se lee del servidor
TOLERANCIA_FDS = 10   # diferencia admitida de descriptores abiertos tras la carga
PING = b"GET /ping HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"


class Fallo(Exception):
    pass


def conectar(puerto, intentos=1):
    ultimo = None
    for _ in range(intentos):
        try:
            return socket.create_connection((HOST, puerto), timeout=ESPERA)
        except OSError as e:
            ultimo = e
            time.sleep(0.1)
    raise Fallo(f"no se puede conectar al puerto {puerto}: {ultimo}")


def esperar_servidor(puerto):
    conectar(puerto, intentos=100).close()


def enviar(datos=b"", puerto=0, cerrar_escritura=True, espera=ESPERA):
    """Envía datos, opcionalmente cierra la escritura y lee hasta EOF o hasta el tiempo de espera.

    Devuelve (respuesta, eof). eof es False si se agotó el tiempo sin que el servidor cerrara.
    """
    s = conectar(puerto)
    try:
        s.settimeout(espera)
        if datos:
            try:
                s.sendall(datos)
            except OSError:
                pass  # el servidor puede cerrar antes de que terminemos de enviar: no es un fallo
        if cerrar_escritura:
            try:
                s.shutdown(socket.SHUT_WR)
            except OSError:
                pass
        out = bytearray()
        while len(out) < MAX_RESP:
            try:
                trozo = s.recv(65536)
            except socket.timeout:
                return bytes(out), False
            except OSError:
                return bytes(out), True
            if not trozo:
                return bytes(out), True
            out += trozo
        return bytes(out), True
    finally:
        s.close()


def reiniciar(puerto, datos):
    """Conexión que se cierra con RST (SO_LINGER 0), tras enviar datos opcionales."""
    s = conectar(puerto)
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
        if datos:
            s.sendall(datos)
    except OSError:
        pass
    finally:
        s.close()


def estado_de(resp):
    partes = resp.split(b"\r\n", 1)[0].split(b" ")
    if len(partes) >= 2 and partes[0].startswith(b"HTTP/"):
        try:
            return int(partes[1])
        except ValueError:
            return None
    return None


def ping(puerto):
    resp, _ = enviar(PING, puerto, cerrar_escritura=False)
    return estado_de(resp), resp


def servidor_vivo(pid):
    # kill -0 da éxito también con procesos zombis; ps distingue el estado Z.
    st = subprocess.run(["ps", "-o", "stat=", "-p", str(pid)], capture_output=True, text=True).stdout
    st = st.strip()
    return bool(st) and not st.startswith("Z")


def contar_fds(pid):
    out = subprocess.run(["lsof", "-nP", "-p", str(pid)], capture_output=True, text=True).stdout
    return max(0, len(out.splitlines()) - 1)


def payload(rng):
    """Devuelve (nombre, bytes) de una petición mal formada o basura aleatoria."""
    k = rng.randrange(12)
    if k == 0:
        return "binaria", rng.randbytes(rng.randint(1, 8192))
    if k == 1:
        return "cabecera_20k", b"GET / HTTP/1.1\r\nX-Grande: " + b"a" * 20000 + b"\r\n\r\n"
    if k == 2:
        return "cl_mayor_que_cuerpo", b"POST /ping HTTP/1.1\r\nContent-Length: 500\r\n\r\nabc"
    if k == 3:
        return "cl_menor_que_cuerpo", b"POST /ping HTTP/1.1\r\nContent-Length: 2\r\n\r\n" + b"abcdef" * 10
    if k == 4:
        valor = rng.choice([b"-1", b"abc", b"99999999999999999999999", b"1e9", b"", b"  7  ", b"+5"])
        return "cl_no_valido", b"POST /ping HTTP/1.1\r\nContent-Length: " + valor + b"\r\n\r\nxyz"
    if k == 5:
        return "sin_crlf", b"GET /ping HTTP/1.1\nHost: x\n\n"
    if k == 6:
        trozos = [b"\r", b"\n", b" ", b":", b"GET ", b"a"]
        return "sin_crlf_mezcla", b"".join(rng.choice(trozos) for _ in range(rng.randint(1, 300)))
    if k == 7:
        metodo = bytes(rng.choice(b"ABCDEFGHIJKLMNOPQRSTUVWXYZ") for _ in range(rng.randint(1, 40)))
        return "metodo_desconocido", metodo + b" /ping HTTP/1.1\r\n\r\n"
    if k == 8:
        return "linea_truncada", b"GET /pi"
    if k == 9:
        return "cabecera_con_nul", b"GET / HTTP/1.1\r\n\x00\x01: \x7f\r\n\r\n"
    if k == 10:
        return "version_invalida", b"GET /ping HTTP/9.9\r\n\r\n"
    return "crlf_repetido", b"\r\n" * rng.randint(1, 2000)


def cmd_garbage(a):
    rng = random.Random(a.seed)
    esperar_servidor(a.puerto)
    estados = {}
    resets = 0
    for i in range(a.n):
        if rng.random() < 0.1:
            reiniciar(a.puerto, rng.randbytes(rng.randint(0, 512)))
            resets += 1
        else:
            nombre, datos = payload(rng)
            try:
                resp, _ = enviar(datos, a.puerto)
            except Fallo as e:
                raise Fallo(f"conexion {i} ({nombre}): {e}")
            clave = str(estado_de(resp)) if estado_de(resp) is not None else "sin_respuesta"
            estados[clave] = estados.get(clave, 0) + 1
        if (i + 1) % 100 == 0 and not servidor_vivo(a.pid):
            raise Fallo(f"el servidor murió tras {i + 1} conexiones")
    if not servidor_vivo(a.pid):
        raise Fallo("el servidor no está vivo tras la basura")
    st, resp = ping(a.puerto)
    print(f"METRICA garbage conexiones={a.n} resets={resets} estados={estados} ping={st}")
    if st != 200 or not resp.endswith(b"\r\n\r\nok"):
        raise Fallo(f"GET /ping tras la basura no devolvió 200 ok (estado {st})")


def cmd_timeout(a):
    esperar_servidor(a.puerto)
    limite = a.timeout_ms / 1000 + 1.0
    # 1) cliente que conecta y no envía nada: debe recibir respuesta o EOF antes del límite
    s = conectar(a.puerto)
    s.settimeout(limite + 5)
    t0 = time.monotonic()
    try:
        while True:
            trozo = s.recv(65536)
            if not trozo:
                break
    except socket.timeout:
        raise Fallo(f"cliente inactivo: el servidor no respondió ni cerró en {limite + 5:.1f} s")
    except OSError:
        pass
    finally:
        t_inactivo = time.monotonic() - t0
        s.close()
    print(f"METRICA timeout_inactivo={t_inactivo:.3f}s limite={limite:.3f}s")
    if t_inactivo >= limite:
        raise Fallo(f"cliente inactivo cerrado en {t_inactivo:.3f} s (límite {limite:.3f} s)")
    # 2) un cliente inactivo retiene al servidor; una petición válida posterior debe atenderse
    idle = conectar(a.puerto)
    try:
        t1 = time.monotonic()
        st, resp = ping(a.puerto)
        t_valido = time.monotonic() - t1
    finally:
        idle.close()
    print(f"METRICA timeout_valido_tras_inactivo={t_valido:.3f}s estado={st}")
    if st != 200:
        raise Fallo(f"petición válida tras cliente inactivo: estado {st}")
    if t_valido >= limite + 0.5:
        raise Fallo(f"petición válida tardó {t_valido:.3f} s (máximo {limite + 0.5:.3f} s)")


def cmd_carga(a):
    esperar_servidor(a.puerto)
    ping(a.puerto)  # calentamiento
    time.sleep(0.2)
    fd0 = contar_fds(a.pid)
    ok = 0
    malos = []
    t0 = time.monotonic()
    for i in range(a.n):
        resp, _ = enviar(PING, a.puerto, cerrar_escritura=False)
        if estado_de(resp) == 200 and resp.endswith(b"\r\n\r\nok"):
            ok += 1
        elif len(malos) < 5:
            malos.append((i, estado_de(resp)))
    dur = time.monotonic() - t0
    time.sleep(0.5)
    fd1 = contar_fds(a.pid)
    print(f"METRICA carga n={a.n} ok={ok} rps={a.n / dur:.1f} duracion={dur:.2f}s "
          f"fds_antes={fd0} fds_despues={fd1}")
    if ok != a.n:
        raise Fallo(f"{a.n - ok} de {a.n} peticiones no fueron 200 ok, muestra {malos}")
    if fd1 - fd0 > TOLERANCIA_FDS:
        raise Fallo(f"fuga de descriptores: {fd0} -> {fd1} (tolerancia {TOLERANCIA_FDS})")


def cmd_grande(a):
    esperar_servidor(a.puerto)
    casos = {
        "ruta_1MB": b"GET /" + b"a" * (1 << 20) + b" HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n",
        "cabecera_1MB": b"GET /ping HTTP/1.1\r\nX: " + b"b" * (1 << 20) + b"\r\nConnection: close\r\n\r\n",
        "cuerpo_1MB": b"POST /ping HTTP/1.1\r\nContent-Length: 1048576\r\n\r\n" + b"c" * (1 << 20),
    }
    for nombre, datos in casos.items():
        resp, _ = enviar(datos, a.puerto)
        print(f"METRICA grande {nombre} estado={estado_de(resp)}")
        if not servidor_vivo(a.pid):
            raise Fallo(f"el servidor cayó con {nombre}")
    st, _ = ping(a.puerto)
    if st != 200:
        raise Fallo(f"GET /ping tras las peticiones grandes: estado {st}")


def cmd_salir(a):
    esperar_servidor(a.puerto)
    resp, _ = enviar(b"GET /salir HTTP/1.1\r\nConnection: close\r\n\r\n", a.puerto, cerrar_escritura=False)
    if estado_de(resp) != 200:
        raise Fallo(f"GET /salir devolvió estado {estado_de(resp)}")


def main():
    p = argparse.ArgumentParser(description="Robustez HTTP del framework web (QA).")
    sub = p.add_subparsers(dest="cmd", required=True)
    for nombre in ["garbage", "timeout", "carga", "grande", "salir"]:
        sp = sub.add_parser(nombre)
        sp.add_argument("--puerto", type=int, required=True)
        sp.add_argument("--pid", type=int, required=True)
        if nombre == "garbage":
            sp.add_argument("--n", type=int, default=1000)
            sp.add_argument("--seed", type=int, default=4242)
        if nombre == "timeout":
            sp.add_argument("--timeout-ms", type=int, default=1000)
        if nombre == "carga":
            sp.add_argument("--n", type=int, default=2000)
    a = p.parse_args()
    if not 23000 <= a.puerto <= 23999:
        print("uso: --puerto debe estar entre 23000 y 23999", file=sys.stderr)
        return 2
    funciones = {"garbage": cmd_garbage, "timeout": cmd_timeout, "carga": cmd_carga,
                 "grande": cmd_grande, "salir": cmd_salir}
    try:
        funciones[a.cmd](a)
    except Fallo as e:
        print(f"FALLO {a.cmd}: {e}")
        return 1
    print(f"OK {a.cmd}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
