#!/usr/bin/env python3
# Genera icons/icon-192.png e icons/icon-512.png sin dependencias (zlib + struct).
# Dibujo: fondo azul (#2563eb) con una "L" blanca centrada, dentro de la zona segura de iconos
# "maskable" (el 80 % central). Uso: python3 lib/app/pwa/generar_iconos.py
import os
import struct
import zlib

FONDO = (0x25, 0x63, 0xEB)
TRAZO = (0xFF, 0xFF, 0xFF)


def png(ancho, pixeles):
    # pixeles: lista de filas, cada fila es bytes RGB
    crudo = b"".join(b"\x00" + fila for fila in pixeles)

    def bloque(tipo, datos):
        cuerpo = tipo + datos
        return struct.pack(">I", len(datos)) + cuerpo + struct.pack(">I", zlib.crc32(cuerpo) & 0xFFFFFFFF)

    cabecera = struct.pack(">IIBBBBB", ancho, ancho, 8, 2, 0, 0, 0)
    return (b"\x89PNG\r\n\x1a\n" + bloque(b"IHDR", cabecera)
            + bloque(b"IDAT", zlib.compress(crudo, 9)) + bloque(b"IEND", b""))


def dibujar(n):
    # Coordenadas en fracciones del lado: la L ocupa 0.30..0.70 horizontal y 0.25..0.75 vertical.
    grosor = 0.12
    filas = []
    for y in range(n):
        fy = (y + 0.5) / n
        fila = bytearray()
        for x in range(n):
            fx = (x + 0.5) / n
            en_vertical = 0.30 <= fx <= 0.30 + grosor and 0.25 <= fy <= 0.75
            en_base = 0.30 <= fx <= 0.70 and 0.75 - grosor <= fy <= 0.75
            color = TRAZO if (en_vertical or en_base) else FONDO
            fila += bytes(color)
        filas.append(bytes(fila))
    return filas


def main():
    carpeta = os.path.join(os.path.dirname(os.path.abspath(__file__)), "icons")
    os.makedirs(carpeta, exist_ok=True)
    for n in (192, 512):
        ruta = os.path.join(carpeta, "icon-%d.png" % n)
        with open(ruta, "wb") as f:
            f.write(png(n, dibujar(n)))
        print(ruta)


if __name__ == "__main__":
    main()
