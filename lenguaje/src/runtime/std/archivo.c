/* Nativos de lib/nucleo/archivo.lg: ficheros con descriptor POSIX (open/read/write/lseek/close).
 * Un descriptor es un entero >= 0; -1 indica error (y deja errno, que lg puede leer con archivo_error). */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lg_rt.h"

/* Copia un LgStr a una cadena C terminada en NUL. El llamador libera. */
static char* a_cstr(const LgStr* s) {
  char* c = (char*)malloc((size_t)s->len + 1);
  if (!c) lg_panic_at(NULL, 0, "memoria agotada");
  memcpy(c, s->data, (size_t)s->len);
  c[s->len] = '\0';
  return c;
}

/* Modos: "r" lectura; "w" escritura (trunca o crea); "a" añadir al final; "rw" lectura y escritura. */
int64_t lg_std_archivo_abrir(const LgStr* ruta, const LgStr* modo) {
  char* r = a_cstr(ruta);
  int flags;
  if (modo->len == 1 && modo->data[0] == 'r') {
    flags = O_RDONLY;
  } else if (modo->len == 1 && modo->data[0] == 'w') {
    flags = O_WRONLY | O_CREAT | O_TRUNC;
  } else if (modo->len == 1 && modo->data[0] == 'a') {
    flags = O_WRONLY | O_CREAT | O_APPEND;
  } else if (modo->len == 2 && modo->data[0] == 'r' && modo->data[1] == 'w') {
    flags = O_RDWR | O_CREAT;
  } else {
    free(r);
    errno = EINVAL;
    return -1;
  }
  int fd = open(r, flags, 0644);
  free(r);
  return fd;
}

int64_t lg_std_archivo_cerrar(int64_t fd) {
  return close((int)fd) == 0 ? 0 : -1;
}

/* Hasta `max` bytes desde la posición actual. "" si no hay más datos o si falla. */
LgStr* lg_std_archivo_leer(int64_t fd, int64_t max) {
  if (max <= 0) return lg_str_new("", 0);
  if (max > (1 << 20)) max = 1 << 20;
  char* buf = (char*)malloc((size_t)max);
  if (!buf) lg_panic_at(NULL, 0, "memoria agotada");
  ssize_t n;
  do {
    n = read((int)fd, buf, (size_t)max);
  } while (n < 0 && errno == EINTR);
  if (n < 0) n = 0;
  LgStr* r = lg_str_new(buf, (int64_t)n);
  free(buf);
  return r;
}

/* Siguiente línea sin "\n" (ni "\r" previo). Al fin de fichero devuelve "": usa archivo_fin. */
LgStr* lg_std_archivo_leer_linea(int64_t fd) {
  size_t cap = 128, len = 0;
  char* buf = (char*)malloc(cap);
  if (!buf) lg_panic_at(NULL, 0, "memoria agotada");
  for (;;) {
    char c;
    ssize_t n = read((int)fd, &c, 1);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    if (c == '\n') break;
    if (len + 1 >= cap) {
      cap *= 2;
      char* nb = (char*)realloc(buf, cap);
      if (!nb) lg_panic_at(NULL, 0, "memoria agotada");
      buf = nb;
    }
    buf[len++] = c;
  }
  if (len > 0 && buf[len - 1] == '\r') len--;
  LgStr* r = lg_str_new(buf, (int64_t)len);
  free(buf);
  return r;
}

/* Escribe todo el texto. Devuelve los bytes escritos, o -1. */
int64_t lg_std_archivo_escribir(int64_t fd, const LgStr* texto) {
  int64_t hecho = 0;
  while (hecho < texto->len) {
    ssize_t n = write((int)fd, texto->data + hecho, (size_t)(texto->len - hecho));
    if (n < 0 && errno == EINTR) continue;
    if (n < 0) return -1;
    hecho += n;
  }
  return hecho;
}

/* 1 si la posición actual está al final del fichero (o el descriptor no admite posición), 0 si quedan datos. */
int64_t lg_std_archivo_fin(int64_t fd) {
  off_t cur = lseek((int)fd, 0, SEEK_CUR);
  if (cur < 0) return 1;
  struct stat st;
  if (fstat((int)fd, &st) != 0) return 1;
  return cur >= st.st_size ? 1 : 0;
}

/* Posición actual en bytes, o -1. */
int64_t lg_std_archivo_posicion(int64_t fd) {
  off_t p = lseek((int)fd, 0, SEEK_CUR);
  return p < 0 ? -1 : (int64_t)p;
}

/* Mueve el cursor a una posición absoluta. Devuelve la nueva posición, o -1. */
int64_t lg_std_archivo_ir(int64_t fd, int64_t pos) {
  off_t p = lseek((int)fd, (off_t)pos, SEEK_SET);
  return p < 0 ? -1 : (int64_t)p;
}

int64_t lg_std_archivo_existe(const LgStr* ruta) {
  char* r = a_cstr(ruta);
  struct stat st;
  int ok = stat(r, &st) == 0;
  free(r);
  return ok ? 1 : 0;
}

/* Tamaño en bytes de un fichero, o -1. */
int64_t lg_std_archivo_tamano(const LgStr* ruta) {
  char* r = a_cstr(ruta);
  struct stat st;
  int ok = stat(r, &st) == 0;
  free(r);
  return ok ? (int64_t)st.st_size : -1;
}

int64_t lg_std_archivo_borrar(const LgStr* ruta) {
  char* r = a_cstr(ruta);
  int ok = unlink(r) == 0;
  free(r);
  return ok ? 0 : -1;
}

/* Mensaje del último error (errno) en texto, p. ej. "No such file or directory". */
LgStr* lg_std_archivo_error(void) {
  const char* m = strerror(errno);
  return lg_str_new(m, (int64_t)strlen(m));
}

/* Lee un fichero entero. "" si no se puede abrir o leer (usa archivo_existe o archivo_error para saber por qué). */
LgStr* lg_std_archivo_leer_todo(const LgStr* ruta) {
  int64_t fd = lg_std_archivo_abrir(ruta, lg_str_new("r", 1));
  if (fd < 0) return lg_str_new("", 0);
  size_t cap = 4096, len = 0;
  char* buf = (char*)malloc(cap);
  if (!buf) lg_panic_at(NULL, 0, "memoria agotada");
  for (;;) {
    if (len == cap) {
      cap *= 2;
      char* nb = (char*)realloc(buf, cap);
      if (!nb) lg_panic_at(NULL, 0, "memoria agotada");
      buf = nb;
    }
    ssize_t n = read((int)fd, buf + len, cap - len);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    len += (size_t)n;
  }
  close((int)fd);
  LgStr* r = lg_str_new(buf, (int64_t)len);
  free(buf);
  return r;
}

/* Escribe (trunca o crea) un fichero entero. 0 si va bien, -1 si no. */
int64_t lg_std_archivo_escribir_todo(const LgStr* ruta, const LgStr* texto) {
  int64_t fd = lg_std_archivo_abrir(ruta, lg_str_new("w", 1));
  if (fd < 0) return -1;
  int64_t n = lg_std_archivo_escribir(fd, texto);
  close((int)fd);
  return n < 0 ? -1 : 0;
}
