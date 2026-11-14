/* Implementación del runtime del lenguaje. La ABI está en lg_rt.h (contrato).
 * Depende solo de libc y libm. Comentarios y mensajes en español.
 *
 * Convención de propiedad (resumen):
 *   - lg_object_new devuelve rc = 1. El que lo recibe es dueño de una referencia.
 *   - destroy(obj) libera los HIJOS del objeto (no su cabecera). La memoria la libera
 *     lg_weak_release cuando también se sueltan las referencias débiles (weak llega a 0).
 *   - lg_list_append / lg_list_set / lg_dict_set toman posesión del slot (no retienen),
 *     salvo la clave str de un diccionario, que sí se retiene (ver lg_dict_set).
 */

#include "lg_rt.h"

/* Funciones de panic adicionales para el codegen (no están aún en lg_rt.h; el integrador
 * debe añadir sus prototipos al header). */
void lg_panic_overflow(const char* file, int64_t line, const char* op);
void lg_panic_div_zero(const char* file, int64_t line);
void lg_panic_index(const char* file, int64_t line, int64_t idx, int64_t len);

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---------- Utilidades internas ---------- */

/* Convierte un slot entero a puntero (inverso de ptrtoint). */
static void* slot_ptr(int64_t slot) { return (void*)(intptr_t)slot; }

/* Panic sin ubicación de fuente (errores detectados dentro del propio runtime). */
static void rt_fail(const char* msg) { lg_panic_at(NULL, 0, msg); }

/* Suma/multiplicación con comprobación de desbordamiento de int64. */
static int64_t ck_add(int64_t a, int64_t b, const char* op) {
  if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) lg_panic_overflow(NULL, 0, op);
  return a + b;
}
static int64_t ck_sub(int64_t a, int64_t b, const char* op) {
  if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) lg_panic_overflow(NULL, 0, op);
  return a - b;
}
static int64_t ck_mul(int64_t a, int64_t b, const char* op) {
  if (a > 0) {
    if (b > 0) {
      if (a > INT64_MAX / b) lg_panic_overflow(NULL, 0, op);
    } else if (b < 0) {
      if (b < INT64_MIN / a) lg_panic_overflow(NULL, 0, op);
    }
  } else if (a < 0) {
    if (b > 0) {
      if (a < INT64_MIN / b) lg_panic_overflow(NULL, 0, op);
    } else if (b < 0) {
      if (a != 0 && b < INT64_MAX / a) lg_panic_overflow(NULL, 0, op);
    }
  }
  return a * b;
}

/* Escritura acotada de texto en buf (siempre termina en '\0'). Devuelve la nueva longitud. */
static size_t app_str(char* buf, size_t cap, size_t n, const char* s) {
  while (*s && n + 1 < cap) buf[n++] = *s++;
  buf[n] = '\0';
  return n;
}
static size_t app_int(char* buf, size_t cap, size_t n, long long v) {
  char tmp[32];
  snprintf(tmp, sizeof tmp, "%lld", v);
  return app_str(buf, cap, n, tmp);
}

/* Formatea una forma como "(2, 3)"; rango 1 como "(3,)" y rango 0 como "()". */
static void fmt_shape(char* buf, size_t cap, int64_t rank, const int64_t* shape) {
  size_t n = app_str(buf, cap, 0, "(");
  for (int64_t i = 0; i < rank; i++) {
    if (i > 0) n = app_str(buf, cap, n, ", ");
    n = app_int(buf, cap, n, (long long)shape[i]);
  }
  if (rank == 1) n = app_str(buf, cap, n, ",");
  app_str(buf, cap, n, ")");
}

/* ---------- Estado global del runtime ---------- */

static int g_argc = 0;
static char** g_argv = NULL;
static int g_atexit_registered = 0;

static void rt_flush_at_exit(void) { fflush(stdout); }

/* ---------- Errores de ejecución ---------- */

void lg_panic_at(const char* file, int64_t line, const char* msg) {
  fflush(stdout);
  if (file)
    fprintf(stderr, "%s:%lld: error de ejecución: %s\n", file, (long long)line, msg ? msg : "");
  else
    fprintf(stderr, "error de ejecución: %s\n", msg ? msg : "");
  fflush(stderr);
  exit(1);
}

void lg_panic_overflow(const char* file, int64_t line, const char* op) {
  char msg[160];
  snprintf(msg, sizeof msg, "desbordamiento de entero en '%s'", op ? op : "?");
  lg_panic_at(file, line, msg);
}

void lg_panic_div_zero(const char* file, int64_t line) {
  lg_panic_at(file, line, "división por cero");
}

void lg_panic_index(const char* file, int64_t line, int64_t idx, int64_t len) {
  char msg[160];
  snprintf(msg, sizeof msg, "índice %lld fuera de rango (longitud %lld)", (long long)idx, (long long)len);
  lg_panic_at(file, line, msg);
}

void lg_panic_float_to_int(const char* file, int64_t line, double v) {
  char msg[160];
  snprintf(msg, sizeof msg, "conversión de float a int fuera de rango: %g", v);
  lg_panic_at(file, line, msg);
}

/* ---------- Objetos con conteo de referencias ---------- */

void* lg_object_new(uint64_t size, lg_destroy_fn destroy) {
  if (size < sizeof(LgHeader)) size = sizeof(LgHeader);
  LgHeader* h = calloc(1, (size_t)size);
  if (!h) rt_fail("memoria agotada");
  h->rc = 1;
  h->destroy = destroy;
  h->weak = 1;  /* el weak implícito de las referencias fuertes */
  return h;
}

/* Suelta una referencia débil (o el weak implícito). Libera la memoria al llegar a 0. */
static void lg_weak_release(void* obj) {
  if (__atomic_fetch_sub(&((LgHeader*)obj)->weak, 1, __ATOMIC_ACQ_REL) == 1) free(obj);
}

/* Conteo atómico (__atomic es portable a clang y gcc en macOS y Linux). Retain no necesita
 * orden de memoria: el objeto ya es accesible por el hilo que lo retiene. Release usa
 * ACQ_REL: el hilo que llega a cero ve todas las escrituras de los demás antes de destruir. */
void lg_retain(void* obj) {
  if (!obj) return;
  __atomic_fetch_add(&((LgHeader*)obj)->rc, 1, __ATOMIC_RELAXED);
}

void lg_release(void* obj) {
  if (!obj) return;
  LgHeader* h = (LgHeader*)obj;
  int64_t viejo = __atomic_fetch_sub(&h->rc, 1, __ATOMIC_ACQ_REL);
  if (viejo <= 0) rt_fail("liberación de un objeto que ya no tiene referencias");
  if (viejo == 1) {
    if (h->destroy) h->destroy(obj);
    lg_weak_release(obj);  /* suelta el weak implícito de las referencias fuertes */
  }
}

/* ---------- Referencias débiles ---------- */

static void debil_destroy(void* p) { lg_weak_release(((LgDebil*)p)->obj); }

LgDebil* lg_debil_new(void* obj) {
  if (!obj) rt_fail("debil() de un valor nulo");
  if (__atomic_load_n(&((LgHeader*)obj)->rc, __ATOMIC_RELAXED) <= 0)
    rt_fail("debil() de un objeto que ya no tiene referencias");
  __atomic_fetch_add(&((LgHeader*)obj)->weak, 1, __ATOMIC_RELAXED);
  LgDebil* d = lg_object_new(sizeof(LgDebil), debil_destroy);
  d->obj = obj;
  return d;
}

/* Intenta pasar de débil a fuerte: solo si rc > 0 (CAS para no revivir un objeto a cero). */
void* lg_debil_fuerte(const LgDebil* d) {
  LgHeader* h = (LgHeader*)d->obj;
  int64_t v = __atomic_load_n(&h->rc, __ATOMIC_RELAXED);
  while (v > 0) {
    if (__atomic_compare_exchange_n(&h->rc, &v, v + 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
      return h;
  }
  return NULL;
}

/* ---------- Cadenas ---------- */


LgStr* lg_str_new(const char* data, int64_t len) {
  if (len < 0) rt_fail("longitud de cadena negativa");
  if (len > 0 && !data) rt_fail("cadena nula con longitud positiva");
  if ((uint64_t)len > SIZE_MAX / 2) rt_fail("cadena demasiado grande");
  LgStr* s = lg_object_new(sizeof(LgStr) + (size_t)len + 1, NULL);
  s->len = len;
  if (len > 0) memcpy(s->data, data, (size_t)len);
  s->data[len] = '\0';
  /* lg_object_new ya dejó la memoria a cero; el '\0' final es explícito por claridad. */
  return s;
}

LgStr* lg_str_concat(const LgStr* a, const LgStr* b) {
  if (!a || !b) rt_fail("concatenación con cadena nula");
  int64_t len = ck_add(a->len, b->len, "+");
  if ((uint64_t)len > SIZE_MAX / 2) rt_fail("cadena demasiado grande");
  LgStr* s = lg_object_new(sizeof(LgStr) + (size_t)len + 1, NULL);
  s->len = len;
  memcpy(s->data, a->data, (size_t)a->len);
  memcpy(s->data + a->len, b->data, (size_t)b->len);
  s->data[len] = '\0';
  return s;
}

int64_t lg_str_eq(const LgStr* a, const LgStr* b) {
  if (a == b) return 1;
  if (!a || !b) return 0;
  return a->len == b->len && memcmp(a->data, b->data, (size_t)a->len) == 0;
}

int64_t lg_str_len(const LgStr* s) { return s ? s->len : 0; }

LgStr* lg_str_from_int(int64_t v) {
  char buf[32];
  snprintf(buf, sizeof buf, "%lld", (long long)v);
  return lg_str_new(buf, (int64_t)strlen(buf));
}

/* Representación corta que reconvierte exactamente al mismo double.
 * Busca el menor número de cifras significativas p (1..17) con %.{p-1}e tal que strtod
 * reproduce el valor. Luego formatea al estilo de Python repr:
 *   - notación fija si el exponente decimal está en [-4, 16)  -> "3.0", "0.1", "100.0"
 *   - notación científica en otro caso                       -> "1e+300", "1e-05"
 * Los valores especiales son "nan", "inf" y "-inf". */
static void float_repr(double v, char* out, size_t cap) {
  if (isnan(v)) {
    snprintf(out, cap, "nan");
    return;
  }
  if (isinf(v)) {
    snprintf(out, cap, "%s", v < 0 ? "-inf" : "inf");
    return;
  }
  char buf[64];
  int p;
  for (p = 1; p <= 17; p++) {
    snprintf(buf, sizeof buf, "%.*e", p - 1, v);
    if (strtod(buf, NULL) == v) break;
  }
  if (p > 17) p = 17; /* con 17 cifras siempre se reconvierte; por seguridad */

  const char* e = strchr(buf, 'e');
  int exp10 = e ? atoi(e + 1) : 0;
  if (exp10 >= -4 && exp10 < 16) {
    int dec = p - 1 - exp10;
    if (dec < 0) dec = 0;
    snprintf(out, cap, "%.*f", dec, v);
    if (!strchr(out, '.') && strlen(out) + 2 < cap) strcat(out, ".0");
  } else {
    snprintf(out, cap, "%s", buf);
  }
}

LgStr* lg_str_from_float(double v) {
  char buf[64];
  float_repr(v, buf, sizeof buf);
  return lg_str_new(buf, (int64_t)strlen(buf));
}

LgStr* lg_str_from_bool(int64_t v) {
  return v ? lg_str_new("true", 4) : lg_str_new("false", 5);
}

static int es_espacio(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/* Texto del valor para mensajes de error (acotado). */
static void vista_previa(const LgStr* s, char* buf, size_t cap) {
  int64_t n = s->len > 40 ? 40 : s->len;
  snprintf(buf, cap, "%.*s", (int)n, s->data);
}

int64_t lg_str_to_int(const LgStr* s) {
  if (!s) rt_fail("conversión de cadena nula a entero");
  const char* fin = s->data + s->len;
  char* end = NULL;
  errno = 0;
  long long v = strtoll(s->data, &end, 10);
  if (end != s->data) {
    while (end < fin && es_espacio(*end)) end++;
  }
  if (end == s->data || end != fin || errno == ERANGE) {
    char pv[64], msg[160];
    vista_previa(s, pv, sizeof pv);
    snprintf(msg, sizeof msg, "no se puede convertir '%s' a int", pv);
    rt_fail(msg);
  }
  return (int64_t)v;
}

double lg_str_to_float(const LgStr* s) {
  if (!s) rt_fail("conversión de cadena nula a float");
  /* Se rechazan notaciones hexadecimales, que strtod acepta pero el lenguaje no. */
  if (memchr(s->data, 'x', (size_t)s->len) || memchr(s->data, 'X', (size_t)s->len)) {
    char pv[64], msg[160];
    vista_previa(s, pv, sizeof pv);
    snprintf(msg, sizeof msg, "no se puede convertir '%s' a float", pv);
    rt_fail(msg);
  }
  const char* fin = s->data + s->len;
  char* end = NULL;
  double v = strtod(s->data, &end);
  if (end != s->data) {
    while (end < fin && es_espacio(*end)) end++;
  }
  if (end == s->data || end != fin) {
    char pv[64], msg[160];
    vista_previa(s, pv, sizeof pv);
    snprintf(msg, sizeof msg, "no se puede convertir '%s' a float", pv);
    rt_fail(msg);
  }
  return v;
}

/* ---------- Listas ---------- */

static void list_destroy(void* p) {
  LgList* l = (LgList*)p;
  if (l->elem_is_heap) {
    for (int64_t i = 0; i < l->len; i++) lg_release(slot_ptr(l->items[i]));
  }
  free(l->items);
}

LgList* lg_list_new(int64_t elem_is_heap, int64_t capacity_hint) {
  LgList* l = lg_object_new(sizeof(LgList), list_destroy);
  l->elem_is_heap = elem_is_heap ? 1 : 0;
  l->cap = capacity_hint > 0 ? capacity_hint : 4;
  if ((uint64_t)l->cap > SIZE_MAX / sizeof(int64_t)) rt_fail("lista demasiado grande");
  l->items = malloc((size_t)l->cap * sizeof(int64_t));
  if (!l->items) rt_fail("memoria agotada");
  return l;
}

/* Garantiza capacidad para `need` elementos; crecimiento geométrico (x2). */
static void list_reserve(LgList* l, int64_t need) {
  if (need <= l->cap) return;
  int64_t ncap = l->cap;
  while (ncap < need) {
    if (ncap > INT64_MAX / 2) rt_fail("lista demasiado grande");
    ncap *= 2;
  }
  if ((uint64_t)ncap > SIZE_MAX / sizeof(int64_t)) rt_fail("lista demasiado grande");
  int64_t* ni = realloc(l->items, (size_t)ncap * sizeof(int64_t));
  if (!ni) rt_fail("memoria agotada");
  l->items = ni;
  l->cap = ncap;
}

/* Normaliza el índice: los negativos cuentan desde el final (como en Python).
 * Panic si queda fuera de [0, len). */
static int64_t list_norm(const LgList* l, int64_t idx) {
  if (idx < -l->len || idx >= l->len) lg_panic_index(NULL, 0, idx, l->len);
  return idx < 0 ? idx + l->len : idx;
}

void lg_list_append(LgList* l, int64_t slot) {
  if (!l) rt_fail("append sobre lista nula");
  list_reserve(l, l->len + 1);
  l->items[l->len++] = slot;
}

int64_t lg_list_get(const LgList* l, int64_t idx) {
  if (!l) rt_fail("acceso a lista nula");
  return l->items[list_norm(l, idx)];
}

void lg_list_set(LgList* l, int64_t idx, int64_t slot) {
  if (!l) rt_fail("set sobre lista nula");
  int64_t i = list_norm(l, idx);
  int64_t viejo = l->items[i];
  l->items[i] = slot; /* primero almacenamos el nuevo, luego soltamos el anterior */
  if (l->elem_is_heap) lg_release(slot_ptr(viejo));
}

int64_t lg_list_len(const LgList* l) { return l ? l->len : 0; }

int64_t lg_list_pop(LgList* l) {
  if (!l) rt_fail("pop sobre lista nula");
  if (l->len == 0) rt_fail("pop de una lista vacía");
  return l->items[--l->len];
}

/* ---------- Diccionarios (orden de inserción) ----------
 * Estructura: array de entradas en orden de inserción + tabla hash abierta (sondeo lineal)
 * que guarda índice de entrada + 1 (0 = vacío). Sin borrados, así que no hacen falta tumbas.
 * Carga máxima 1/2. */

typedef struct DictEntry {
  int64_t key;
  int64_t val;
  uint64_t hash;
} DictEntry;

struct LgDict {
  LgHeader h;
  int64_t key_kind;     /* LG_KEY_INT, LG_KEY_STR o LG_KEY_BOOL */
  int64_t val_is_heap;  /* 1 si los valores son punteros con rc */
  int64_t len;          /* número de entradas */
  int64_t cap;          /* capacidad del array de entradas */
  DictEntry* entries;
  int64_t tsize;        /* tamaño de la tabla, potencia de 2 */
  int64_t* table;
};

static uint64_t mezcla64(uint64_t x) {
  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33;
  return x;
}

static uint64_t clave_hash(const LgDict* d, int64_t k) {
  switch (d->key_kind) {
    case LG_KEY_STR: {
      const LgStr* s = slot_ptr(k);
      if (!s) return 0;
      uint64_t h = 1469598103934665603ULL; /* FNV-1a */
      for (int64_t i = 0; i < s->len; i++) {
        h ^= (unsigned char)s->data[i];
        h *= 1099511628211ULL;
      }
      return mezcla64(h);
    }
    case LG_KEY_BOOL:
      return mezcla64(k != 0 ? 1u : 0u);
    default:
      return mezcla64((uint64_t)k);
  }
}

static int clave_igual(const LgDict* d, int64_t a, int64_t b) {
  switch (d->key_kind) {
    case LG_KEY_STR:
      return (int)lg_str_eq(slot_ptr(a), slot_ptr(b));
    case LG_KEY_BOOL:
      return (a != 0) == (b != 0);
    default:
      return a == b;
  }
}

static void tabla_insertar(LgDict* d, uint64_t h, int64_t idx) {
  uint64_t mask = (uint64_t)d->tsize - 1;
  uint64_t pos = h & mask;
  while (d->table[pos] != 0) pos = (pos + 1) & mask;
  d->table[pos] = idx + 1;
}

/* Índice de la entrada con esa clave, o -1 si no existe. */
static int64_t dict_buscar(const LgDict* d, int64_t key, uint64_t h) {
  uint64_t mask = (uint64_t)d->tsize - 1;
  uint64_t pos = h & mask;
  for (;;) {
    int64_t slot = d->table[pos];
    if (slot == 0) return -1;
    const DictEntry* e = &d->entries[slot - 1];
    if (e->hash == h && clave_igual(d, e->key, key)) return slot - 1;
    pos = (pos + 1) & mask;
  }
}

static void dict_destroy(void* p) {
  LgDict* d = (LgDict*)p;
  for (int64_t i = 0; i < d->len; i++) {
    if (d->key_kind == LG_KEY_STR) lg_release(slot_ptr(d->entries[i].key));
    if (d->val_is_heap) lg_release(slot_ptr(d->entries[i].val));
  }
  free(d->entries);
  free(d->table);
}

LgDict* lg_dict_new(int64_t key_kind, int64_t val_is_heap) {
  if (key_kind != LG_KEY_INT && key_kind != LG_KEY_STR && key_kind != LG_KEY_BOOL)
    rt_fail("tipo de clave de diccionario desconocido");
  LgDict* d = lg_object_new(sizeof(LgDict), dict_destroy);
  d->key_kind = key_kind;
  d->val_is_heap = val_is_heap ? 1 : 0;
  d->cap = 8;
  d->entries = malloc((size_t)d->cap * sizeof(DictEntry));
  d->tsize = 16;
  d->table = calloc((size_t)d->tsize, sizeof(int64_t));
  if (!d->entries || !d->table) rt_fail("memoria agotada");
  return d;
}

/* Asegura sitio para una entrada más: crece el array y, si hace falta, reconstruye la tabla. */
static void dict_reservar_una(LgDict* d) {
  if (d->len == d->cap) {
    if (d->cap > INT64_MAX / 2 || (uint64_t)(d->cap * 2) > SIZE_MAX / sizeof(DictEntry))
      rt_fail("diccionario demasiado grande");
    DictEntry* ne = realloc(d->entries, (size_t)(d->cap * 2) * sizeof(DictEntry));
    if (!ne) rt_fail("memoria agotada");
    d->entries = ne;
    d->cap *= 2;
  }
  if ((d->len + 1) * 2 > d->tsize) {
    int64_t nts = d->tsize * 2;
    while ((d->len + 1) * 2 > nts) {
      if (nts > INT64_MAX / 4) rt_fail("diccionario demasiado grande");
      nts *= 2;
    }
    int64_t* nt = calloc((size_t)nts, sizeof(int64_t));
    if (!nt) rt_fail("memoria agotada");
    free(d->table);
    d->table = nt;
    d->tsize = nts;
    for (int64_t i = 0; i < d->len; i++) tabla_insertar(d, d->entries[i].hash, i);
  }
}

/* Propiedad:
 *   - val: se toma posesión (no se retiene), igual que en las listas.
 *   - key en diccionarios STR: si la entrada es nueva, el diccionario RETIENE la clave
 *     (lg_retain), así que el llamador conserva su propia referencia y debe soltarla
 *     cuando ya no la use. Si la clave ya existía, no se retiene nada: se conserva la
 *     clave original y el valor anterior se libera (si val_is_heap).
 *   - key INT y BOOL no tienen propiedad. */
void lg_dict_set(LgDict* d, int64_t key_slot, int64_t val_slot) {
  if (!d) rt_fail("set sobre diccionario nulo");
  if (d->key_kind == LG_KEY_BOOL) key_slot = key_slot != 0;
  uint64_t h = clave_hash(d, key_slot);
  int64_t i = dict_buscar(d, key_slot, h);
  if (i >= 0) {
    int64_t viejo = d->entries[i].val;
    d->entries[i].val = val_slot;
    if (d->val_is_heap) lg_release(slot_ptr(viejo));
    return;
  }
  if (d->key_kind == LG_KEY_STR) lg_retain(slot_ptr(key_slot));
  dict_reservar_una(d);
  DictEntry* e = &d->entries[d->len];
  e->key = key_slot;
  e->val = val_slot;
  e->hash = h;
  d->len++;
  tabla_insertar(d, h, d->len - 1);
}

int64_t lg_dict_get(const LgDict* d, int64_t key_slot, int64_t* found) {
  if (!d) rt_fail("acceso a diccionario nulo");
  int64_t i = dict_buscar(d, key_slot, clave_hash(d, key_slot));
  if (found) *found = i >= 0;
  return i >= 0 ? d->entries[i].val : 0;
}

int64_t lg_dict_get_or_panic(const LgDict* d, int64_t key_slot) {
  int64_t found = 0;
  int64_t v = lg_dict_get(d, key_slot, &found);
  if (!found) rt_fail("clave no encontrada");
  return v;
}

int64_t lg_dict_has(const LgDict* d, int64_t key_slot) {
  if (!d) rt_fail("acceso a diccionario nulo");
  return dict_buscar(d, key_slot, clave_hash(d, key_slot)) >= 0;
}

int64_t lg_dict_len(const LgDict* d) { return d ? d->len : 0; }

static int64_t dict_norm_pos(const LgDict* d, int64_t i) {
  if (!d) rt_fail("acceso a diccionario nulo");
  if (i < 0 || i >= d->len) {
    char msg[160];
    snprintf(msg, sizeof msg, "posición %lld fuera de rango en diccionario de %lld entradas",
             (long long)i, (long long)d->len);
    rt_fail(msg);
  }
  return i;
}

int64_t lg_dict_key_at(const LgDict* d, int64_t i) { return d->entries[dict_norm_pos(d, i)].key; }

int64_t lg_dict_val_at(const LgDict* d, int64_t i) { return d->entries[dict_norm_pos(d, i)].val; }

/* ---------- Tensores ---------- */

static void tensor_destroy(void* p) {
  LgTensor* t = (LgTensor*)p;
  free(t->shape);
  free(t->data);
}

static const char* nombre_dtype(int64_t dtype) { return dtype == LG_DT_FLOAT ? "float" : "int"; }

/* Reserva un tensor con datos a cero. Valida rango, dimensiones y desbordamiento del tamaño. */
static LgTensor* tensor_nuevo(int64_t dtype, int64_t rank, const int64_t* shape) {
  if (dtype != LG_DT_FLOAT && dtype != LG_DT_INT) rt_fail("tipo de elemento de tensor desconocido");
  if (rank < 0) rt_fail("rango de tensor negativo");
  if (rank > 0 && !shape) rt_fail("forma nula para tensor de rango positivo");
  int64_t numel = 1;
  for (int64_t i = 0; i < rank; i++) {
    if (shape[i] < 0) {
      char msg[120];
      snprintf(msg, sizeof msg, "dimensión negativa (%lld) en la forma del tensor", (long long)shape[i]);
      rt_fail(msg);
    }
    numel = ck_mul(numel, shape[i], "*");
  }
  if ((uint64_t)numel > SIZE_MAX / sizeof(int64_t)) rt_fail("tensor demasiado grande");

  LgTensor* t = lg_object_new(sizeof(LgTensor), tensor_destroy);
  t->dtype = dtype;
  t->rank = rank;
  t->numel = numel;
  t->shape = malloc((size_t)(rank > 0 ? rank : 1) * sizeof(int64_t));
  t->data = calloc((size_t)(numel > 0 ? numel : 1), sizeof(int64_t));
  if (!t->shape || !t->data) rt_fail("memoria agotada");
  for (int64_t i = 0; i < rank; i++) t->shape[i] = shape[i];
  return t;
}

static double elem_float(const LgTensor* t, int64_t i) {
  return t->dtype == LG_DT_FLOAT ? ((const double*)t->data)[i] : (double)((const int64_t*)t->data)[i];
}

static int64_t elem_int(const LgTensor* t, int64_t i) {
  return t->dtype == LG_DT_INT ? ((const int64_t*)t->data)[i] : (int64_t)((const double*)t->data)[i];
}

/* Generador xorshift64* propio. La semilla viene de LG_SEED (decimal) o es 42 por defecto. */
static uint64_t g_rng = 0;
static int g_rng_listo = 0;

static uint64_t rng_siguiente(void) {
  if (!g_rng_listo) {
    uint64_t semilla = 42;
    const char* env = getenv("LG_SEED");
    if (env && *env) {
      char* fin = NULL;
      unsigned long long v = strtoull(env, &fin, 10);
      if (fin && *fin == '\0') semilla = (uint64_t)v;
    }
    g_rng = semilla ? semilla : 0x9E3779B97F4A7C15ULL;
    g_rng_listo = 1;
  }
  uint64_t x = g_rng;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  g_rng = x;
  return x * 2685821657736338717ULL;
}

/* Uniforme en (0, 1]: nunca es 0, así que log() es seguro. */
static double rng_uniforme(void) {
  return (double)((rng_siguiente() >> 11) + 1) / 9007199254740992.0; /* 2^53 */
}

LgTensor* lg_tensor_zeros(int64_t dtype, int64_t rank, const int64_t* shape) {
  return tensor_nuevo(dtype, rank, shape);
}

LgTensor* lg_tensor_ones(int64_t dtype, int64_t rank, const int64_t* shape) {
  LgTensor* t = tensor_nuevo(dtype, rank, shape);
  for (int64_t i = 0; i < t->numel; i++) {
    if (dtype == LG_DT_FLOAT)
      ((double*)t->data)[i] = 1.0;
    else
      ((int64_t*)t->data)[i] = 1;
  }
  return t;
}

/* Normal estándar por Box-Muller. Para dtype INT el valor se redondea al entero más próximo. */
LgTensor* lg_tensor_randn(int64_t dtype, int64_t rank, const int64_t* shape) {
  LgTensor* t = tensor_nuevo(dtype, rank, shape);
  const double dos_pi = 6.283185307179586476925286766559;
  for (int64_t i = 0; i < t->numel; i++) {
    double u1 = rng_uniforme();
    double u2 = rng_uniforme();
    double z = sqrt(-2.0 * log(u1)) * cos(dos_pi * u2);
    if (dtype == LG_DT_FLOAT)
      ((double*)t->data)[i] = z;
    else
      ((int64_t*)t->data)[i] = (int64_t)llround(z);
  }
  return t;
}

/* Los slots de la lista son bits de double para FLOAT y el entero directo para INT. */
LgTensor* lg_tensor_from_list(int64_t dtype, LgList* values, int64_t rank, const int64_t* shape) {
  if (!values) rt_fail("from_list con lista nula");
  LgTensor* t = tensor_nuevo(dtype, rank, shape);
  if (values->len != t->numel) {
    char fs[64], msg[256];
    fmt_shape(fs, sizeof fs, rank, shape);
    snprintf(msg, sizeof msg, "la lista tiene %lld elementos pero la forma %s requiere %lld",
             (long long)values->len, fs, (long long)t->numel);
    rt_fail(msg);
  }
  for (int64_t i = 0; i < t->numel; i++) {
    int64_t slot = values->items[i];
    if (dtype == LG_DT_FLOAT) {
      double d;
      memcpy(&d, &slot, sizeof d);
      ((double*)t->data)[i] = d;
    } else {
      ((int64_t*)t->data)[i] = slot;
    }
  }
  return t;
}

static void mismo_dtype(const LgTensor* a, const LgTensor* b, const char* op) {
  if (!a || !b) rt_fail("operación sobre tensor nulo");
  if (a->dtype != b->dtype) {
    char msg[160];
    snprintf(msg, sizeof msg, "tipos de elemento distintos en '%s': %s y %s", op,
             nombre_dtype(a->dtype), nombre_dtype(b->dtype));
    rt_fail(msg);
  }
}

LgTensor* lg_tensor_matmul(const LgTensor* a, const LgTensor* b) {
  mismo_dtype(a, b, "@");
  if (a->rank != 2 || b->rank != 2) rt_fail("'@' requiere tensores de rango 2");
  if (a->shape[1] != b->shape[0]) {
    char fa[64], fb[64], msg[256];
    fmt_shape(fa, sizeof fa, a->rank, a->shape);
    fmt_shape(fb, sizeof fb, b->rank, b->shape);
    snprintf(msg, sizeof msg, "formas incompatibles para @: %s y %s", fa, fb);
    rt_fail(msg);
  }
  int64_t m = a->shape[0], k = a->shape[1], n = b->shape[1];
  int64_t out_shape[2] = {m, n};
  LgTensor* out = tensor_nuevo(a->dtype, 2, out_shape);
  for (int64_t i = 0; i < m; i++) {
    for (int64_t j = 0; j < n; j++) {
      if (a->dtype == LG_DT_FLOAT) {
        double acc = 0.0;
        for (int64_t p = 0; p < k; p++) acc += elem_float(a, i * k + p) * elem_float(b, p * n + j);
        ((double*)out->data)[i * n + j] = acc;
      } else {
        int64_t acc = 0;
        for (int64_t p = 0; p < k; p++)
          acc = ck_add(acc, ck_mul(elem_int(a, i * k + p), elem_int(b, p * n + j), "@"), "@");
        ((int64_t*)out->data)[i * n + j] = acc;
      }
    }
  }
  return out;
}

/* Operación elemento a elemento sobre dos tensores de la misma forma. op: '+', '-' o '*'. */
static LgTensor* binario(const LgTensor* a, const LgTensor* b, char op) {
  char sym[2] = {op, '\0'};
  mismo_dtype(a, b, sym);
  int misma_forma = a->rank == b->rank;
  for (int64_t i = 0; misma_forma && i < a->rank; i++) misma_forma = a->shape[i] == b->shape[i];
  if (!misma_forma) {
    char fa[64], fb[64], msg[256];
    fmt_shape(fa, sizeof fa, a->rank, a->shape);
    fmt_shape(fb, sizeof fb, b->rank, b->shape);
    snprintf(msg, sizeof msg, "formas incompatibles para %s: %s y %s", sym, fa, fb);
    rt_fail(msg);
  }
  LgTensor* out = tensor_nuevo(a->dtype, a->rank, a->shape);
  for (int64_t i = 0; i < a->numel; i++) {
    if (a->dtype == LG_DT_FLOAT) {
      double x = elem_float(a, i), y = elem_float(b, i);
      ((double*)out->data)[i] = op == '+' ? x + y : op == '-' ? x - y : x * y;
    } else {
      int64_t x = elem_int(a, i), y = elem_int(b, i);
      ((int64_t*)out->data)[i] = op == '+' ? ck_add(x, y, sym)
                                 : op == '-' ? ck_sub(x, y, sym)
                                             : ck_mul(x, y, sym);
    }
  }
  return out;
}

LgTensor* lg_tensor_add(const LgTensor* a, const LgTensor* b) { return binario(a, b, '+'); }
LgTensor* lg_tensor_sub(const LgTensor* a, const LgTensor* b) { return binario(a, b, '-'); }
LgTensor* lg_tensor_mul(const LgTensor* a, const LgTensor* b) { return binario(a, b, '*'); }

LgTensor* lg_tensor_transpose(const LgTensor* a) {
  if (!a) rt_fail("transpose de tensor nulo");
  if (a->rank != 2) rt_fail("transpose requiere un tensor de rango 2");
  int64_t m = a->shape[0], n = a->shape[1];
  int64_t out_shape[2] = {n, m};
  LgTensor* out = tensor_nuevo(a->dtype, 2, out_shape);
  for (int64_t i = 0; i < m; i++)
    for (int64_t j = 0; j < n; j++) {
      if (a->dtype == LG_DT_FLOAT)
        ((double*)out->data)[j * m + i] = ((const double*)a->data)[i * n + j];
      else
        ((int64_t*)out->data)[j * m + i] = ((const int64_t*)a->data)[i * n + j];
    }
  return out;
}

int64_t lg_tensor_rank(const LgTensor* t) { return t ? t->rank : 0; }

int64_t lg_tensor_dim(const LgTensor* t, int64_t i) {
  if (!t) rt_fail("dim de tensor nulo");
  if (i < 0 || i >= t->rank) {
    char msg[120];
    snprintf(msg, sizeof msg, "dimensión %lld fuera de rango para tensor de rango %lld",
             (long long)i, (long long)t->rank);
    rt_fail(msg);
  }
  return t->shape[i];
}

int64_t lg_tensor_numel(const LgTensor* t) { return t ? t->numel : 0; }

LgList* lg_tensor_shape_list(const LgTensor* t) {
  if (!t) rt_fail("shape de tensor nulo");
  LgList* l = lg_list_new(0, t->rank);
  for (int64_t i = 0; i < t->rank; i++) lg_list_append(l, t->shape[i]);
  return l;
}

static void comprobar_indice_plano(const LgTensor* t, int64_t idx) {
  if (!t) rt_fail("acceso a tensor nulo");
  if (idx < 0 || idx >= t->numel) {
    char msg[160];
    snprintf(msg, sizeof msg, "índice plano %lld fuera de rango para tensor de %lld elementos",
             (long long)idx, (long long)t->numel);
    rt_fail(msg);
  }
}

/* Lectura con conversión: un tensor INT se lee como double y viceversa (truncando). */
double lg_tensor_get_float(const LgTensor* t, int64_t flat_index) {
  comprobar_indice_plano(t, flat_index);
  return elem_float(t, flat_index);
}

int64_t lg_tensor_get_int(const LgTensor* t, int64_t flat_index) {
  comprobar_indice_plano(t, flat_index);
  return elem_int(t, flat_index);
}

/* Normaliza el índice idx de la dimensión d (negativos desde el final); panic con ubicación si está fuera. */
static int64_t normalizar_dim(const LgTensor* t, int64_t d, int64_t idx, const char* file, int64_t line) {
  int64_t dim = t->shape[d];
  if (idx < -dim || idx >= dim) {
    char msg[200];
    snprintf(msg, sizeof msg, "índice %lld fuera de rango (dimensión %lld de tamaño %lld)", (long long)idx,
             (long long)d, (long long)dim);
    lg_panic_at(file, line, msg);
  }
  return idx < 0 ? idx + dim : idx;
}

/* Índice plano del elemento (o bloque) que empieza en los n primeros índices. n <= rank. */
static int64_t indice_plano(const LgTensor* t, int64_t n, const int64_t* idx, const char* file, int64_t line) {
  if (!t) lg_panic_at(file, line, "acceso a tensor nulo");
  if (n < 0 || n > t->rank) {
    char msg[160];
    snprintf(msg, sizeof msg, "%lld índices para un tensor de rango %lld", (long long)n, (long long)t->rank);
    lg_panic_at(file, line, msg);
  }
  int64_t flat = 0;
  for (int64_t d = 0; d < n; d++) {
    int64_t stride = 1;
    for (int64_t k = d + 1; k < t->rank; k++) stride *= t->shape[k];
    flat += normalizar_dim(t, d, idx[d], file, line) * stride;
  }
  return flat;
}

/* Comprueba que n sea el rango completo (índice de un elemento) y devuelve su índice plano. */
static int64_t indice_elemento(const LgTensor* t, int64_t n, const int64_t* idx, const char* file, int64_t line) {
  if (!t) lg_panic_at(file, line, "acceso a tensor nulo");
  if (n != t->rank) lg_panic_at(file, line, "el índice de un tensor debe dar todas sus dimensiones");
  return indice_plano(t, n, idx, file, line);
}

double lg_tensor_index_float(const LgTensor* t, const char* file, int64_t line, int64_t n, const int64_t* indices) {
  return lg_tensor_get_float(t, indice_elemento(t, n, indices, file, line));
}

int64_t lg_tensor_index_int(const LgTensor* t, const char* file, int64_t line, int64_t n, const int64_t* indices) {
  return lg_tensor_get_int(t, indice_elemento(t, n, indices, file, line));
}

void lg_tensor_set_float(LgTensor* t, const char* file, int64_t line, int64_t n, const int64_t* indices, double v) {
  int64_t flat = indice_elemento(t, n, indices, file, line);
  if (t->dtype == LG_DT_FLOAT) ((double*)t->data)[flat] = v;
  else ((int64_t*)t->data)[flat] = (int64_t)v;
}

void lg_tensor_set_int(LgTensor* t, const char* file, int64_t line, int64_t n, const int64_t* indices, int64_t v) {
  int64_t flat = indice_elemento(t, n, indices, file, line);
  if (t->dtype == LG_DT_INT) ((int64_t*)t->data)[flat] = v;
  else ((double*)t->data)[flat] = (double)v;
}

LgTensor* lg_tensor_slice(const LgTensor* t, const char* file, int64_t line, int64_t n, const int64_t* indices) {
  int64_t flat = indice_plano(t, n, indices, file, line);
  int64_t block = 1;
  for (int64_t k = n; k < t->rank; k++) block *= t->shape[k];
  LgTensor* out = tensor_nuevo(t->dtype, t->rank - n, t->shape + n);
  if (t->dtype == LG_DT_FLOAT) memcpy(out->data, (const double*)t->data + flat, (size_t)block * sizeof(double));
  else memcpy(out->data, (const int64_t*)t->data + flat, (size_t)block * sizeof(int64_t));
  return out;
}

static const char* simbolo_op(int64_t op) {
  static const char* simbolos[] = {"+", "-", "*", "/"};
  return op >= 0 && op <= 3 ? simbolos[op] : "?";
}

LgTensor* lg_tensor_scale_float(const LgTensor* t, double s, int64_t op, int64_t scalar_left, const char* file,
                                int64_t line) {
  if (!t) lg_panic_at(file, line, "operación con tensor nulo");
  if (t->dtype != LG_DT_FLOAT) lg_panic_at(file, line, "lg_tensor_scale_float requiere Tensor[float]");
  if (op == 3 && !scalar_left && s == 0.0) lg_panic_div_zero(file, line);
  LgTensor* out = tensor_nuevo(LG_DT_FLOAT, t->rank, t->shape);
  const double* in = t->data;
  double* o = out->data;
  for (int64_t i = 0; i < t->numel; i++) {
    double x = in[i];
    double a = scalar_left ? s : x, b = scalar_left ? x : s;
    double r = 0.0;
    switch (op) {
      case 0: r = a + b; break;
      case 1: r = a - b; break;
      case 2: r = a * b; break;
      default: r = a / b; break;  /* el divisor escalar cero ya se comprobó; un elemento cero sigue IEEE */
    }
    o[i] = r;
  }
  return out;
}

LgTensor* lg_tensor_scale_int(const LgTensor* t, int64_t s, int64_t op, int64_t scalar_left, const char* file,
                              int64_t line) {
  if (!t) lg_panic_at(file, line, "operación con tensor nulo");
  if (t->dtype != LG_DT_INT) lg_panic_at(file, line, "lg_tensor_scale_int requiere Tensor[int]");
  if (op == 3 && !scalar_left && s == 0) lg_panic_div_zero(file, line);
  LgTensor* out = tensor_nuevo(LG_DT_INT, t->rank, t->shape);
  const int64_t* in = t->data;
  int64_t* o = out->data;
  for (int64_t i = 0; i < t->numel; i++) {
    int64_t x = in[i];
    int64_t a = scalar_left ? s : x, b = scalar_left ? x : s;
    int64_t r = 0;
    int ov = 0;
    switch (op) {
      case 0: ov = __builtin_add_overflow(a, b, &r); break;
      case 1: ov = __builtin_sub_overflow(a, b, &r); break;
      case 2: ov = __builtin_mul_overflow(a, b, &r); break;
      default:
        if (b == 0) lg_panic_div_zero(file, line);
        ov = a == INT64_MIN && b == -1;
        if (!ov) r = a / b;
        break;
    }
    if (ov) lg_panic_overflow(file, line, simbolo_op(op));
    o[i] = r;
  }
  return out;
}

void lg_tensor_check_shape(const LgTensor* t, int64_t rank, const int64_t* shape, const char* file,
                           int64_t line) {
  if (!t) lg_panic_at(file, line, "tensor nulo al comprobar la forma");
  int ok = t->rank == rank;
  for (int64_t i = 0; ok && i < rank; i++) ok = t->shape[i] == shape[i];
  if (!ok) {
    char esperada[64], real[64], msg[256];
    fmt_shape(esperada, sizeof esperada, rank, shape);
    fmt_shape(real, sizeof real, t->rank, t->shape);
    snprintf(msg, sizeof msg, "forma incompatible: se esperaba %s pero el tensor tiene forma %s",
             esperada, real);
    lg_panic_at(file, line, msg);
  }
}

/* Imprime recursivamente por filas: [[a, b], [c, d]]. base es el índice plano del bloque. */
static void imprimir_rec(const LgTensor* t, int64_t dim, int64_t base) {
  if (dim == t->rank) {
    if (t->dtype == LG_DT_FLOAT) {
      char buf[64];
      float_repr(((const double*)t->data)[base], buf, sizeof buf);
      fputs(buf, stdout);
    } else {
      printf("%lld", (long long)((const int64_t*)t->data)[base]);
    }
    return;
  }
  int64_t stride = 1;
  for (int64_t j = dim + 1; j < t->rank; j++) stride *= t->shape[j];
  putchar('[');
  for (int64_t i = 0; i < t->shape[dim]; i++) {
    if (i > 0) fputs(", ", stdout);
    imprimir_rec(t, dim + 1, base + i * stride);
  }
  putchar(']');
}

void lg_tensor_print(const LgTensor* t) {
  if (!t) {
    fputs("Tensor[nulo]\n", stdout);
    return;
  }
  char fs[64];
  fmt_shape(fs, sizeof fs, t->rank, t->shape);
  printf("Tensor[%s, %s]\n", nombre_dtype(t->dtype), fs);
  if (t->numel == 0 && t->rank > 0) {
    fputs("[]\n", stdout);
    return;
  }
  imprimir_rec(t, 0, 0);
  putchar('\n');
}

/* ---------- Impresión ---------- */

void lg_print_int(int64_t v) { printf("%lld", (long long)v); }

void lg_print_float(double v) {
  char buf[64];
  float_repr(v, buf, sizeof buf);
  fputs(buf, stdout);
}

void lg_print_bool(int64_t v) { fputs(v ? "true" : "false", stdout); }

void lg_print_str(const LgStr* s) {
  if (!s) return;
  fwrite(s->data, 1, (size_t)s->len, stdout);
}

void lg_print_text(const char* s) {
  if (s) fputs(s, stdout);
}

void lg_print_space(void) { putchar(' '); }

void lg_print_newline(void) { putchar('\n'); }

/* ---------- Inicialización ---------- */

#ifndef __wasm__  /* WebAssembly: sin señales; el desbordamiento de pila es una trampa del motor */
/* Manejador de SIGSEGV/SIGBUS: en una recursión profunda la pila se agota y el fault llega aquí.
 * Corre en la pila alternativa (sigaltstack), así que solo usa funciones async-signal-safe (write, _exit). */
static uintptr_t g_stack_lo = 0; /* límites de la pila principal, calculados al inicio */
static uintptr_t g_stack_hi = 0;

static void rt_fault_handler(int sig, siginfo_t* si, void* uctx) {
  (void)sig;
  (void)uctx;
  uintptr_t addr = (uintptr_t)si->si_addr;
  /* Margen de 1 MiB por debajo del límite: la página de guarda puede quedar justo debajo. */
  int is_stack = g_stack_hi != 0 && addr <= g_stack_hi && addr + ((uintptr_t)1 << 20) >= g_stack_lo;
  if (is_stack) {
    static const char m[] = "error de ejecución: desbordamiento de la pila (recursión demasiado profunda)\n";
    (void)write(2, m, sizeof m - 1);
  } else {
    static const char m[] = "error de ejecución: fallo de memoria (segmentación)\n";
    (void)write(2, m, sizeof m - 1);
  }
  _exit(1);
}

#define LG_ALTSTACK_SIZE ((size_t)64 * 1024)

/* En Linux, pthread_getattr_np y sigaction/SA_SIGINFO son extensiones de glibc: hay que
 * compilar con _GNU_SOURCE (lo define CMake). */
#if defined(__linux__) && !defined(_GNU_SOURCE)
#error "lg_rt.c en Linux requiere -D_GNU_SOURCE (pthread_getattr_np); lo define CMakeLists.txt"
#endif

static void rt_install_fault_handlers(void) {
  /* Límites de la pila del hilo principal. */
#if defined(__APPLE__)
  pthread_t self = pthread_self();
  g_stack_hi = (uintptr_t)pthread_get_stackaddr_np(self);
  g_stack_lo = g_stack_hi - pthread_get_stacksize_np(self);
#elif defined(__linux__)
  pthread_attr_t at;
  void* base = NULL;
  size_t sz = 0;
  if (pthread_getattr_np(pthread_self(), &at) == 0) {
    if (pthread_attr_getstack(&at, &base, &sz) == 0) {
      g_stack_lo = (uintptr_t)base;
      g_stack_hi = g_stack_lo + sz;
    }
    pthread_attr_destroy(&at);
  }
#endif

  /* Pila alternativa reservada al inicio: sin ella no se puede manejar el desbordamiento. */
  void* mem = malloc(LG_ALTSTACK_SIZE);
  if (!mem) return;
  stack_t ss;
  memset(&ss, 0, sizeof ss);
  ss.ss_sp = mem;
  ss.ss_size = LG_ALTSTACK_SIZE;
  if (sigaltstack(&ss, NULL) != 0) return;

  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = rt_fault_handler;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGBUS, &sa, NULL);
}
#endif

void lg_runtime_init(int argc, char** argv) {
  g_argc = argc;
  g_argv = argv;
  if (!g_atexit_registered) {
    atexit(rt_flush_at_exit);
    g_atexit_registered = 1;
#ifndef __wasm__
    rt_install_fault_handlers();
#endif
  }
}
