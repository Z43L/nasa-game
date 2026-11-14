/* Hilos (Hilo[T]) y canales (Canal[T]) del runtime. Usan pthread: compatible con macOS y Linux.
 * No se soporta Windows. La ABI está en lg_rt.h, sección "Hilos y canales".
 *
 * Modelo: un hilo por llamada (sin ejecutor ni cooperación). Los valores pasan como slots
 * (int64). Los objetos heap usan el conteo atómico de lg_rt.c: retain/release pueden llamarse
 * desde cualquier hilo. Los propios objetos (listas, diccionarios...) NO tienen cerrojos: compartir
 * uno mutable entre hilos es una carrera de datos; el canal es el único punto de sincronización. */

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lg_rt.h"

/* Pila de cada hilo: la del sistema (512 KiB en macOS) es escasa para recursión. */
#define LG_HILO_STACK ((size_t)8 * 1024 * 1024)

struct LgHilo {
  LgHeader h;
  pthread_t tid;
  pthread_mutex_t mu;      /* protege la unión (join) */
  int unido;               /* 1 si ya se hizo pthread_join */
  int64_t (*fn)(int64_t* args);
  int64_t* args;           /* copia de los slots de argumentos (propiedad del hilo) */
  int64_t nargs;
  int64_t resultado;       /* slot del resultado (escrito por el hilo) */
  int64_t result_is_heap;
};

struct LgCanal {
  LgHeader h;
  pthread_mutex_t mu;
  pthread_cond_t hay_datos;  /* se señala al enviar y al cerrar */
  int64_t* buf;              /* cola circular de slots */
  int64_t cap;
  int64_t head;
  int64_t len;
  int cerrado;
  int64_t elem_is_heap;
};

static void hilo_fallo(const char* msg) {
  lg_panic_at(NULL, 0, msg);
}

/* ---------- Hilos ---------- */

static void* hilo_main(void* p) {
  LgHilo* h = (LgHilo*)p;
  h->resultado = h->fn(h->args);  /* el envoltorio libera los argumentos heap */
  free(h->args);
  h->args = NULL;
  return NULL;
}

static void hilo_destroy(void* p) {
  LgHilo* h = (LgHilo*)p;
  /* Soltar un hilo sin esperarlo espera a que termine: su memoria y argumentos siguen en uso. */
  pthread_mutex_lock(&h->mu);
  if (!h->unido) {
    pthread_join(h->tid, NULL);
    h->unido = 1;
  }
  pthread_mutex_unlock(&h->mu);
  if (h->result_is_heap) lg_release((void*)(intptr_t)h->resultado);
  pthread_mutex_destroy(&h->mu);
}

LgHilo* lg_hilo_lanzar(int64_t (*fn)(int64_t* args), const int64_t* args, int64_t nargs, int64_t result_is_heap) {
  if (!fn) hilo_fallo("hilo sin función");
  LgHilo* h = lg_object_new(sizeof(LgHilo), hilo_destroy);
  h->fn = fn;
  h->nargs = nargs;
  h->result_is_heap = result_is_heap;
  h->args = malloc((size_t)(nargs > 0 ? nargs : 1) * sizeof(int64_t));
  if (!h->args) hilo_fallo("memoria agotada");
  if (nargs > 0) memcpy(h->args, args, (size_t)nargs * sizeof(int64_t));
  pthread_mutex_init(&h->mu, NULL);

  pthread_attr_t at;
  pthread_attr_init(&at);
  pthread_attr_setstacksize(&at, LG_HILO_STACK);
  int rc = pthread_create(&h->tid, &at, hilo_main, h);
  pthread_attr_destroy(&at);
  if (rc != 0) hilo_fallo("no se pudo crear un hilo");
  return h;
}

int64_t lg_hilo_esperar(LgHilo* h) {
  pthread_mutex_lock(&h->mu);
  if (!h->unido) {
    pthread_join(h->tid, NULL);
    h->unido = 1;
  }
  pthread_mutex_unlock(&h->mu);
  if (h->result_is_heap) lg_retain((void*)(intptr_t)h->resultado);
  return h->resultado;
}

/* ---------- Canales ---------- */

static void canal_destroy(void* p) {
  LgCanal* c = (LgCanal*)p;
  if (c->elem_is_heap) {
    for (int64_t i = 0; i < c->len; i++) {
      int64_t idx = (c->head + i) % c->cap;
      lg_release((void*)(intptr_t)c->buf[idx]);
    }
  }
  free(c->buf);
  pthread_cond_destroy(&c->hay_datos);
  pthread_mutex_destroy(&c->mu);
}

LgCanal* lg_canal_nuevo(int64_t elem_is_heap) {
  LgCanal* c = lg_object_new(sizeof(LgCanal), canal_destroy);
  c->cap = 8;
  c->buf = malloc((size_t)c->cap * sizeof(int64_t));
  if (!c->buf) hilo_fallo("memoria agotada");
  c->elem_is_heap = elem_is_heap;
  pthread_mutex_init(&c->mu, NULL);
  pthread_cond_init(&c->hay_datos, NULL);
  return c;
}

void lg_canal_enviar(LgCanal* c, int64_t slot, const char* file, int64_t line) {
  pthread_mutex_lock(&c->mu);
  if (c->cerrado) {
    pthread_mutex_unlock(&c->mu);
    lg_panic_at(file, line, "envío en un canal cerrado");
  }
  if (c->len == c->cap) {  /* crece y reordena la cola */
    int64_t ncap = c->cap * 2;
    int64_t* nb = malloc((size_t)ncap * sizeof(int64_t));
    if (!nb) hilo_fallo("memoria agotada");
    for (int64_t i = 0; i < c->len; i++) nb[i] = c->buf[(c->head + i) % c->cap];
    free(c->buf);
    c->buf = nb;
    c->cap = ncap;
    c->head = 0;
  }
  c->buf[(c->head + c->len) % c->cap] = slot;
  c->len++;
  pthread_cond_signal(&c->hay_datos);
  pthread_mutex_unlock(&c->mu);
}

int64_t lg_canal_recibir(LgCanal* c, int64_t* ok) {
  pthread_mutex_lock(&c->mu);
  while (c->len == 0 && !c->cerrado) pthread_cond_wait(&c->hay_datos, &c->mu);
  if (c->len == 0) {  /* cerrado y vacío */
    pthread_mutex_unlock(&c->mu);
    *ok = 0;
    return 0;
  }
  int64_t slot = c->buf[c->head];
  c->head = (c->head + 1) % c->cap;
  c->len--;
  pthread_mutex_unlock(&c->mu);
  *ok = 1;
  return slot;
}

void lg_canal_cerrar(LgCanal* c) {
  pthread_mutex_lock(&c->mu);
  c->cerrado = 1;
  pthread_cond_broadcast(&c->hay_datos);
  pthread_mutex_unlock(&c->mu);
}
