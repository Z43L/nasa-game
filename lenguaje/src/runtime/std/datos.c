/* Nativos de stdlib/datos.lg. Contratos en la cabecera de stdlib/datos.lg. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lg_rt.h"

#define LG_STD_MAX_TOKEN 128

/* Lee el fichero entero. Devuelve un buffer malloc (terminado en '\0') y su longitud en *n.
 * Panic si no se puede abrir o leer. */
static char* lg_std_leer_fichero(const char* ruta, size_t* n) {
  FILE* f = fopen(ruta, "rb");
  if (!f) {
    char msg[512];
    snprintf(msg, sizeof msg, "no se puede abrir para leer: '%s'", ruta);
    lg_panic_at(NULL, 0, msg);
  }
  size_t cap = 4096, len = 0;
  char* buf = malloc(cap + 1);
  if (!buf) lg_panic_at(NULL, 0, "memoria agotada");
  size_t r;
  while ((r = fread(buf + len, 1, cap - len, f)) > 0) {
    len += r;
    if (len == cap) {
      cap *= 2;
      char* nb = realloc(buf, cap + 1);
      if (!nb) lg_panic_at(NULL, 0, "memoria agotada");
      buf = nb;
    }
  }
  int err = ferror(f);
  fclose(f);
  if (err) {
    free(buf);
    char msg[512];
    snprintf(msg, sizeof msg, "error al leer '%s'", ruta);
    lg_panic_at(NULL, 0, msg);
  }
  buf[len] = '\0';
  *n = len;
  return buf;
}

/* Convierte un token (sin separadores) a double. Panic si no es un número completo. */
static double lg_std_token_a_real(const char* ini, const char* fin) {
  while (ini < fin && isspace((unsigned char)*ini)) ini++;
  while (fin > ini && isspace((unsigned char)fin[-1])) fin--;
  size_t len = (size_t)(fin - ini);
  char tok[LG_STD_MAX_TOKEN];
  if (len >= sizeof tok) {
    char msg[256];
    snprintf(msg, sizeof msg, "valor demasiado largo en CSV (%zu caracteres)", len);
    lg_panic_at(NULL, 0, msg);
  }
  memcpy(tok, ini, len);
  tok[len] = '\0';
  char* end = NULL;
  double v = strtod(tok, &end);
  if (end == tok || *end != '\0') {
    char msg[256];
    snprintf(msg, sizeof msg, "valor no numérico en CSV: '%s'", tok);
    lg_panic_at(NULL, 0, msg);
  }
  return v;
}

/* Lee un CSV numérico: los separadores son ',', '\n' y '\r'. Los campos vacíos se ignoran.
 * Devuelve una List[float] nueva (propiedad del llamador). */
LgList* lg_std_leer_csv_numeros(const LgStr* ruta) {
  if (!ruta) lg_panic_at(NULL, 0, "leer_csv_numeros: ruta nula");
  size_t n = 0;
  char* buf = lg_std_leer_fichero(ruta->data, &n);
  LgList* lista = lg_list_new(0, 16);
  const char* ini = buf;
  const char* fin = buf + n;
  for (const char* p = buf; p <= fin; p++) {
    if (p == fin || *p == ',' || *p == '\n' || *p == '\r') {
      const char* a = ini;
      while (a < p && isspace((unsigned char)*a)) a++;
      if (a < p) {
        double v = lg_std_token_a_real(ini, p);
        int64_t slot;
        memcpy(&slot, &v, sizeof slot);
        lg_list_append(lista, slot);
      }
      ini = p + 1;
    }
  }
  free(buf);
  return lista;
}

/* Escribe `texto` en `ruta` (sobrescribe). Panic si no se puede escribir. */
void lg_std_escribir_texto(const LgStr* ruta, const LgStr* texto) {
  if (!ruta || !texto) lg_panic_at(NULL, 0, "escribir_texto: argumento nulo");
  FILE* f = fopen(ruta->data, "wb");
  if (!f) {
    char msg[512];
    snprintf(msg, sizeof msg, "no se puede abrir para escribir: '%s'", ruta->data);
    lg_panic_at(NULL, 0, msg);
  }
  size_t escritos = fwrite(texto->data, 1, (size_t)texto->len, f);
  int cerrado = fclose(f);
  if (escritos != (size_t)texto->len || cerrado != 0) {
    char msg[512];
    snprintf(msg, sizeof msg, "error al escribir '%s'", ruta->data);
    lg_panic_at(NULL, 0, msg);
  }
}

/* Lee el fichero completo como str UTF-8 (sin validar). Devuelve un str nuevo (propiedad del llamador). */
LgStr* lg_std_leer_texto(const LgStr* ruta) {
  if (!ruta) lg_panic_at(NULL, 0, "leer_texto: ruta nula");
  size_t n = 0;
  char* buf = lg_std_leer_fichero(ruta->data, &n);
  LgStr* s = lg_str_new(buf, (int64_t)n);
  free(buf);
  return s;
}
