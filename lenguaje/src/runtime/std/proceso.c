/* Nativos de lib/nucleo/proceso.lg: lanzar comandos del sistema y hablar con procesos por tuberías FIFO.
 * Contratos en la cabecera de proceso.lg. */
#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(__wasi__)
#include <sys/wait.h>
#endif

#include "lg_rt.h"

/* Un FIFO cerrado por el otro extremo no debe matar al servidor: se trata como error de escritura. */
static void ignorar_sigpipe(void) {
  static int hecho = 0;
  if (!hecho) {
    signal(SIGPIPE, SIG_IGN);
    hecho = 1;
  }
}

/* Ejecuta el comando con /bin/sh. Devuelve el código de salida, o -1 si no se pudo lanzar. */
int64_t lg_std_proceso_ejecutar(const LgStr* cmd) {
#if defined(__wasi__)
  (void)cmd;  /* WASI no tiene procesos */
  return -1;
#else
  char* c = (char*)malloc((size_t)cmd->len + 1);
  if (!c) lg_panic_at(NULL, 0, "memoria agotada");
  memcpy(c, cmd->data, (size_t)cmd->len);
  c[cmd->len] = '\0';
  int st = system(c);
  free(c);
  if (st == -1 || !WIFEXITED(st)) return -1;
  return WEXITSTATUS(st);
#endif
}

/* Abre un fichero (o FIFO) con modo "r" o "w". Devuelve un manejador o -1. Bloquea en FIFOs hasta el otro extremo. */
int64_t lg_std_proceso_abrir(const LgStr* ruta, const LgStr* modo) {
  ignorar_sigpipe();
  char* r = (char*)malloc((size_t)ruta->len + 1);
  char* m = (char*)malloc((size_t)modo->len + 1);
  if (!r || !m) lg_panic_at(NULL, 0, "memoria agotada");
  memcpy(r, ruta->data, (size_t)ruta->len);
  r[ruta->len] = '\0';
  memcpy(m, modo->data, (size_t)modo->len);
  m[modo->len] = '\0';
  FILE* f = fopen(r, m);
  free(r);
  free(m);
  return f ? (int64_t)(intptr_t)f : -1;
}

/* Siguiente línea sin el salto final. Al final de fichero devuelve "" (usa proceso_fin para distinguirlo). */
LgStr* lg_std_proceso_leer_linea(int64_t h) {
  FILE* f = (FILE*)(intptr_t)h;
  if (!f) return lg_str_new("", 0);
  char* linea = NULL;
  size_t cap = 0;
  ssize_t n = getline(&linea, &cap, f);
  if (n < 0) {
    free(linea);
    return lg_str_new("", 0);
  }
  int64_t len = (int64_t)n;
  if (len > 0 && linea[len - 1] == '\n') len--;
  if (len > 0 && linea[len - 1] == '\r') len--;
  LgStr* r = lg_str_new(linea, len);
  free(linea);
  return r;
}

/* 1 si el manejador ya llegó a fin de fichero (el otro extremo se cerró), 0 si no. */
int64_t lg_std_proceso_fin(int64_t h) {
  FILE* f = (FILE*)(intptr_t)h;
  if (!f) return 1;
  if (feof(f)) return 1;
  int c = fgetc(f);
  if (c == EOF) return 1;
  ungetc(c, f);
  return 0;
}

/* Escribe texto y lo vacía. Devuelve 0, o -1 si falla (p. ej. el otro extremo ya no lee). */
int64_t lg_std_proceso_escribir(int64_t h, const LgStr* texto) {
  FILE* f = (FILE*)(intptr_t)h;
  if (!f) return -1;
  if (fwrite(texto->data, 1, (size_t)texto->len, f) != (size_t)texto->len) return -1;
  return fflush(f) == 0 ? 0 : -1;
}

void lg_std_proceso_cerrar(int64_t h) {
  FILE* f = (FILE*)(intptr_t)h;
  if (f) fclose(f);
}
