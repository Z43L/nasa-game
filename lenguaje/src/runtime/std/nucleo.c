/* Nativos de lib/nucleo (módulos texto, tiempo, aleatorio, terminal).
 * Contratos en la cabecera de cada módulo .lg. */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lg_rt.h"

/* ---------- texto ---------- */

/* Buffer de crecimiento para construir cadenas nuevas. */
typedef struct NucBuf {
  char* d;
  int64_t len;
  int64_t cap;
} NucBuf;

static void buf_init(NucBuf* b, int64_t cap_inicial) {
  b->len = 0;
  b->cap = cap_inicial > 16 ? cap_inicial : 16;
  b->d = (char*)malloc((size_t)b->cap);
  if (!b->d) lg_panic_at(NULL, 0, "memoria agotada");
}

static void buf_add(NucBuf* b, const char* p, int64_t n) {
  if (n <= 0) return;
  if (b->len + n > b->cap) {
    int64_t nuevo = b->cap * 2;
    if (nuevo < b->len + n) nuevo = b->len + n;
    char* d = (char*)realloc(b->d, (size_t)nuevo);
    if (!d) lg_panic_at(NULL, 0, "memoria agotada");
    b->d = d;
    b->cap = nuevo;
  }
  memcpy(b->d + b->len, p, (size_t)n);
  b->len += n;
}

/* Posición de la primera aparición de `aguja` en `s` desde `desde`, o -1. */
static int64_t buscar_desde(const LgStr* s, const LgStr* aguja, int64_t desde) {
  if (aguja->len == 0) return desde <= s->len ? desde : -1;
  for (int64_t j = desde; j + aguja->len <= s->len; j++) {
    if (s->data[j] == aguja->data[0] && memcmp(s->data + j, aguja->data, (size_t)aguja->len) == 0)
      return j;
  }
  return -1;
}

static int es_espacio(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

LgStr* lg_std_texto_recortar(const LgStr* s) {
  int64_t ini = 0, fin = s->len;
  while (ini < fin && es_espacio(s->data[ini])) ini++;
  while (fin > ini && es_espacio(s->data[fin - 1])) fin--;
  return lg_str_new(s->data + ini, fin - ini);
}

/* Partes separadas por `sep`. Sin separador en s: una sola parte igual a s. s vacía: [""]. */
LgList* lg_std_texto_dividir(const LgStr* s, const LgStr* sep) {
  if (sep->len == 0) lg_panic_at(NULL, 0, "texto_dividir: separador vacío");
  LgList* out = lg_list_new(1, 4);
  int64_t i = 0;
  for (;;) {
    int64_t pos = buscar_desde(s, sep, i);
    if (pos < 0) {
      lg_list_append(out, (int64_t)(intptr_t)lg_str_new(s->data + i, s->len - i));
      break;
    }
    lg_list_append(out, (int64_t)(intptr_t)lg_str_new(s->data + i, pos - i));
    i = pos + sep->len;
  }
  return out;
}

LgStr* lg_std_texto_unir(const LgList* partes, const LgStr* sep) {
  int64_t n = lg_list_len(partes);
  if (n == 0) return lg_str_new("", 0);
  NucBuf b;
  buf_init(&b, 64);
  for (int64_t i = 0; i < n; i++) {
    if (i > 0) buf_add(&b, sep->data, sep->len);
    const LgStr* p = (const LgStr*)(intptr_t)lg_list_get(partes, i);
    buf_add(&b, p->data, p->len);
  }
  LgStr* r = lg_str_new(b.d, b.len);
  free(b.d);
  return r;
}

LgStr* lg_std_texto_reemplazar(const LgStr* s, const LgStr* buscado, const LgStr* nuevo) {
  if (buscado->len == 0) lg_panic_at(NULL, 0, "texto_reemplazar: patrón buscado vacío");
  NucBuf b;
  buf_init(&b, s->len + 16);
  int64_t i = 0;
  for (;;) {
    int64_t pos = buscar_desde(s, buscado, i);
    if (pos < 0) break;
    buf_add(&b, s->data + i, pos - i);
    buf_add(&b, nuevo->data, nuevo->len);
    i = pos + buscado->len;
  }
  buf_add(&b, s->data + i, s->len - i);
  LgStr* r = lg_str_new(b.d, b.len);
  free(b.d);
  return r;
}

/* Solo ASCII: los bytes UTF-8 no ASCII (>= 0x80) se copian sin cambios. */
LgStr* lg_std_texto_a_mayusculas(const LgStr* s) {
  char* d = (char*)malloc((size_t)s->len + 1);
  if (!d) lg_panic_at(NULL, 0, "memoria agotada");
  for (int64_t i = 0; i < s->len; i++) {
    char c = s->data[i];
    d[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
  }
  LgStr* r = lg_str_new(d, s->len);
  free(d);
  return r;
}

LgStr* lg_std_texto_a_minusculas(const LgStr* s) {
  char* d = (char*)malloc((size_t)s->len + 1);
  if (!d) lg_panic_at(NULL, 0, "memoria agotada");
  for (int64_t i = 0; i < s->len; i++) {
    char c = s->data[i];
    d[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
  }
  LgStr* r = lg_str_new(d, s->len);
  free(d);
  return r;
}

/* Devuelven 1 o 0 (el wrapper de lib/nucleo/texto.lg los convierte a bool). */
int64_t lg_std_texto_empieza_por(const LgStr* s, const LgStr* prefijo) {
  return prefijo->len <= s->len && memcmp(s->data, prefijo->data, (size_t)prefijo->len) == 0;
}

int64_t lg_std_texto_termina_por(const LgStr* s, const LgStr* sufijo) {
  return sufijo->len <= s->len &&
         memcmp(s->data + (s->len - sufijo->len), sufijo->data, (size_t)sufijo->len) == 0;
}

int64_t lg_std_texto_contiene(const LgStr* s, const LgStr* aguja) {
  return buscar_desde(s, aguja, 0) >= 0;
}

LgStr* lg_std_texto_repetir(const LgStr* s, int64_t n) {
  if (n < 0) lg_panic_at(NULL, 0, "texto_repetir: n negativo");
  if (n == 0 || s->len == 0) return lg_str_new("", 0);
  if (n > INT64_MAX / s->len) lg_panic_overflow(NULL, 0, "texto_repetir");
  int64_t total = s->len * n;
  char* d = (char*)malloc((size_t)total + 1);
  if (!d) lg_panic_at(NULL, 0, "memoria agotada");
  for (int64_t i = 0; i < n; i++) memcpy(d + i * s->len, s->data, (size_t)s->len);
  LgStr* r = lg_str_new(d, total);
  free(d);
  return r;
}

/* ---------- tiempo ---------- */

int64_t lg_std_tiempo_monotonico_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);
}

int64_t lg_std_tiempo_pared_unix_s(void) {
  return (int64_t)time(NULL);
}

void lg_std_tiempo_dormir_ms(int64_t ms) {
  if (ms <= 0) return;
  struct timespec req, rem;
  req.tv_sec = (time_t)(ms / 1000);
  req.tv_nsec = (long)((ms % 1000) * 1000000);
  while (nanosleep(&req, &rem) != 0 && errno == EINTR) req = rem;
}

/* ---------- aleatorio (xorshift64*, estado propio) ---------- */

static uint64_t estado_aleatorio = 0x9E3779B97F4A7C15ULL;

static uint64_t siguiente_aleatorio(void) {
  uint64_t x = estado_aleatorio;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  estado_aleatorio = x;
  return x * 0x2545F4914F6CDD1DULL;
}

/* La semilla pasa por splitmix64 para que semillas pequeñas den secuencias distintas. */
void lg_std_aleatorio_sembrar(int64_t semilla) {
  uint64_t z = (uint64_t)semilla + 0x9E3779B97F4A7C15ULL;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  z ^= z >> 31;
  estado_aleatorio = z ? z : 1;
}

/* Inclusivo en [a, b]. Rechazo de muestras para evitar sesgo de módulo. */
int64_t lg_std_aleatorio_entero(int64_t a, int64_t b) {
  if (a > b) lg_panic_at(NULL, 0, "aleatorio_entero: a es mayor que b");
  uint64_t span = (uint64_t)b - (uint64_t)a;
  if (span == UINT64_MAX) return (int64_t)siguiente_aleatorio();
  uint64_t n = span + 1;
  uint64_t r;
  do {
    r = siguiente_aleatorio();
  } while (r >= UINT64_MAX - (UINT64_MAX % n));
  return (int64_t)((uint64_t)a + r % n);
}

double lg_std_aleatorio_real(void) {
  return (double)(siguiente_aleatorio() >> 11) * (1.0 / 9007199254740992.0);
}

/* ---------- terminal ---------- */

/* 1 si queda entrada por leer en stdin, 0 si está en EOF. No consume datos. */
int64_t lg_std_terminal_hay_entrada(void) {
  int c = getc(stdin);
  if (c == EOF) return 0;
  ungetc(c, stdin);
  return 1;
}

/* Lee una línea sin el salto final ('\n', y '\r' si lo precede). "" al EOF. Sin límite de longitud. */
LgStr* lg_std_terminal_leer_linea(void) {
  char* linea = NULL;
  size_t cap = 0;
  ssize_t n = getline(&linea, &cap, stdin);
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

/* Códigos 0..7 -> negro, rojo, verde, amarillo, azul, magenta, cian, blanco (SGR 30..37).
 * Otro código: el texto se devuelve sin secuencias. */
LgStr* lg_std_terminal_color(const LgStr* texto, int64_t color) {
  if (color < 0 || color > 7) return lg_str_new(texto->data, texto->len);
  char pre[16];
  int np = snprintf(pre, sizeof pre, "\x1b[%lldm", (long long)(30 + color));
  NucBuf b;
  buf_init(&b, texto->len + 32);
  buf_add(&b, pre, np);
  buf_add(&b, texto->data, texto->len);
  buf_add(&b, "\x1b[0m", 4);
  LgStr* r = lg_str_new(b.d, b.len);
  free(b.d);
  return r;
}

LgStr* lg_std_terminal_limpiar_pantalla(void) {
  return lg_str_new("\x1b[2J\x1b[H", 7);
}
