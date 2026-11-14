/* Nativos de stdlib/tcp.lg. Contratos en la cabecera de stdlib/tcp.lg.
 *
 * Los errores de red no detienen el programa: devuelven -1 (o "" al leer), para que un servidor
 * pueda seguir atendiendo otras conexiones. */
#ifndef __wasm__  /* WebAssembly (frontend): sin sockets */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "lg_rt.h"

/* macOS no tiene MSG_NOSIGNAL: se usa SO_NOSIGPIPE en el socket. En el resto, MSG_NOSIGNAL en send. */
#ifdef MSG_NOSIGNAL
#define LG_SEND_FLAGS MSG_NOSIGNAL
#else
#define LG_SEND_FLAGS 0
#endif

int64_t lg_std_tcp_escuchar(int64_t puerto) {
  if (puerto <= 0 || puerto > 65535) return -1;  /* 0 no vale: no se puede saber el puerto asignado */
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#ifdef SO_NOSIGPIPE
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons((uint16_t)puerto);
  if (bind(fd, (struct sockaddr*)&addr, sizeof addr) < 0 || listen(fd, 64) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

int64_t lg_std_tcp_aceptar(int64_t fd) {
  int cli = accept((int)fd, NULL, NULL);
  if (cli < 0) return -1;
#ifdef SO_NOSIGPIPE
  int one = 1;
  setsockopt(cli, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
  return cli;
}

/* Lee hasta `maximo` bytes de una sola llamada a recv. Devuelve "" si hay error o cierre. */
LgStr* lg_std_tcp_leer(int64_t fd, int64_t maximo) {
  if (maximo <= 0) return lg_str_new("", 0);
  if (maximo > (1 << 20)) maximo = 1 << 20;  /* se acota, no se descarta: el dato queda en el socket */
  char* buf = malloc((size_t)maximo);
  if (!buf) lg_panic_at(NULL, 0, "memoria agotada");
  ssize_t n = recv((int)fd, buf, (size_t)maximo, 0);
  if (n < 0) n = 0;
  LgStr* s = lg_str_new(buf, (int64_t)n);
  free(buf);
  return s;
}

/* Envía todo el texto. Devuelve los bytes enviados o -1 si falla. */
int64_t lg_std_tcp_escribir(int64_t fd, const LgStr* texto) {
  if (!texto) return -1;
  const char* p = texto->data;
  size_t restante = (size_t)texto->len;
  size_t total = 0;
  while (restante > 0) {
    ssize_t n = send((int)fd, p, restante, LG_SEND_FLAGS);
    if (n <= 0) return -1;
    p += n;
    restante -= (size_t)n;
    total += (size_t)n;
  }
  return (int64_t)total;
}

void lg_std_tcp_cerrar(int64_t fd) {
  if (fd >= 0) close((int)fd);
}

/* Conecta por TCP a host:puerto (host en notación numérica IPv4 o "localhost"). Devuelve el fd o -1. */
int64_t lg_std_tcp_conectar(const LgStr* host, int64_t puerto) {
  if (!host || puerto <= 0 || puerto > 65535) return -1;
  const char* h = strcmp(host->data, "localhost") == 0 ? "127.0.0.1" : host->data;
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((uint16_t)puerto);
  if (inet_pton(AF_INET, h, &addr.sin_addr) != 1) return -1;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
#ifdef SO_NOSIGPIPE
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
  if (connect(fd, (struct sockaddr*)&addr, sizeof addr) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

/* Fija el tiempo máximo de espera de lectura y escritura (ms). Devuelve 0 o -1. Evita que un cliente
 * que no envía nada bloquee el servidor para siempre. */
int64_t lg_std_tcp_fijar_tiempo(int64_t conexion, int64_t ms) {
  if (ms <= 0) return -1;
  struct timeval tv;
  tv.tv_sec = (time_t)(ms / 1000);
  tv.tv_usec = (suseconds_t)((ms % 1000) * 1000);
  if (setsockopt((int)conexion, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) < 0) return -1;
  if (setsockopt((int)conexion, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv) < 0) return -1;
  return 0;
}

#endif  /* __wasm__ */
