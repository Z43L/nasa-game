/* CONTRATO (propietario: integrador). ABI del runtime de lenguaje.
 * El codegen emite llamadas a estas funciones; el runtime (lg_rt.c) las implementa.
 * Todos los valores que caben en 64 bits se pasan como "slot" (int64_t) en listas y diccionarios:
 *   int   -> directo
 *   float -> bits de double (memcpy)
 *   bool  -> 0 o 1
 *   heap  -> puntero convertido a int64 (ptrtoint)
 */
#ifndef LG_RT_H
#define LG_RT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Objetos con conteo de referencias ---------- */
typedef void (*lg_destroy_fn)(void* obj);

/* CAMBIO DE ABI (v2, referencias débiles): la cabecera tiene ahora tres campos de 64 bits.
 * Antes era {rc, destroy} (16 bytes); el payload de todo objeto empieza en el byte 24.
 * El codegen declara sus structs LLVM con esta misma cabecera (cellTy, optTy, cloTy y los
 * structs de usuario en codegen.cpp): el payload empieza tras el campo weak.
 *   rc:      referencias fuertes. Atómico (__atomic).
 *   destroy: libera los HIJOS del objeto (campos heap, elementos). NO libera la memoria del
 *            objeto: eso lo hace el runtime (lg_weak_release) cuando ya no quedan débiles.
 *            Puede ser NULL.
 *   weak:    referencias débiles + 1 implícito mientras rc > 0. Al llegar rc a 0 se libera
 *            ese 1 (lg_release); la memoria se libera al llegar weak a 0.
 * Así un Debil[T] puede observar un objeto ya destruido (rc == 0) sin tocar memoria liberada. */
typedef struct LgHeader {
  int64_t rc;
  lg_destroy_fn destroy;
  int64_t weak;
} LgHeader;

/* Reserva un objeto de `size` bytes (incluida la cabecera), rc = 1, weak = 1. Devuelve el puntero a la cabecera. */
void* lg_object_new(uint64_t size, lg_destroy_fn destroy);  /* i64 en el IR (codegen), también en wasm32 */
void lg_retain(void* obj);   /* NULL-seguro */
void lg_release(void* obj);  /* NULL-seguro; si rc llega a 0 llama a destroy y suelta el weak implícito */

/* ---------- Referencias débiles (Debil[T]) ---------- */
/* Caja heap que guarda un objeto sin poseerlo. Su destroy suelta la referencia débil al objeto. */
typedef struct LgDebil {
  LgHeader h;
  void* obj;  /* objeto observado; la memoria sigue viva mientras la caja exista */
} LgDebil;

/* debil(x): obj debe ser un objeto heap vivo (rc > 0). Devuelve una caja nueva con rc = 1. */
LgDebil* lg_debil_new(void* obj);
/* d.fuerte(): devuelve obj con +1 si todavía vive, o NULL si ya se liberó (rc == 0). */
void* lg_debil_fuerte(const LgDebil* d);

/* ---------- Errores de ejecución ---------- */
/* Imprime "archivo:línea: error de ejecución: msg" en stderr y termina con código 1. */
void lg_panic_at(const char* file, int64_t line, const char* msg);
/* Errores con ubicación. file puede ser NULL (entonces solo se imprime el mensaje). */
void lg_panic_overflow(const char* file, int64_t line, const char* op);  /* "desbordamiento de entero en 'op'" */
void lg_panic_div_zero(const char* file, int64_t line);                  /* "división por cero" */
void lg_panic_index(const char* file, int64_t line, int64_t idx, int64_t len);
/* int(f) con f float fuera de [-2^63, 2^63) o NaN.
 * Imprime "file:line: error de ejecución: conversión de float a int fuera de rango: <v>" y termina con 1.
 * Lo llama el codegen antes de FPToSI (que sería comportamiento indefinido). */
void lg_panic_float_to_int(const char* file, int64_t line, double v);

/* ---------- Cadenas (str, UTF-8 inmutable) ---------- */
typedef struct LgStr {
  LgHeader h;
  int64_t len;     /* bytes */
  char data[];     /* terminada en '\0' (len no cuenta el '\0') */
} LgStr;

LgStr* lg_str_new(const char* data, int64_t len);
LgStr* lg_str_concat(const LgStr* a, const LgStr* b);
int64_t lg_str_eq(const LgStr* a, const LgStr* b);     /* 1 o 0 */
int64_t lg_str_len(const LgStr* s);
LgStr* lg_str_from_int(int64_t v);
LgStr* lg_str_from_float(double v);     /* formato: 3.0, 0.1, 1e+300 (repr corta) */
LgStr* lg_str_from_bool(int64_t v);     /* "true" / "false" */
int64_t lg_str_to_int(const LgStr* s);  /* panic si no es entero válido */
double lg_str_to_float(const LgStr* s); /* panic si no es número válido */

/* ---------- Listas (List[T]) ---------- */
typedef struct LgList {
  LgHeader h;
  int64_t len;
  int64_t cap;
  int64_t elem_is_heap;  /* 1 si los slots son punteros con rc: se liberan al destruir la lista */
  int64_t* items;        /* slots */
} LgList;

LgList* lg_list_new(int64_t elem_is_heap, int64_t capacity_hint);
void lg_list_append(LgList* l, int64_t slot);   /* toma posesión del slot (no retiene) */
int64_t lg_list_get(const LgList* l, int64_t idx);     /* panic si fuera de rango; NO retiene */
void lg_list_set(LgList* l, int64_t idx, int64_t slot); /* libera el valor anterior si es heap */
int64_t lg_list_len(const LgList* l);
int64_t lg_list_pop(LgList* l);                 /* quita el último y devuelve su slot (propiedad al llamador) */

/* ---------- Diccionarios (Dict[K, V], orden de inserción) ---------- */
/* key_kind: LG_KEY_INT, LG_KEY_STR, LG_KEY_BOOL */
#define LG_KEY_INT 0
#define LG_KEY_STR 1
#define LG_KEY_BOOL 2

typedef struct LgDict LgDict;  /* definido en lg_rt.c */

LgDict* lg_dict_new(int64_t key_kind, int64_t val_is_heap);
/* Semántica de posesión (IMPORTANTE para el codegen):
 *   - val_slot: el diccionario TOMA posesión (no retiene).
 *   - key_slot de tipo str (LG_KEY_STR): el diccionario RETIENE la clave solo si la entrada es nueva;
 *     el llamador conserva su propia referencia y debe liberarla (lg_release) tras la llamada.
 *   - Al sobrescribir una clave existente se conserva la clave original y se libera el valor anterior. */
void lg_dict_set(LgDict* d, int64_t key_slot, int64_t val_slot);
int64_t lg_dict_get(const LgDict* d, int64_t key_slot, int64_t* found); /* found: 1/0. NO retiene */
int64_t lg_dict_get_or_panic(const LgDict* d, int64_t key_slot);      /* panic si la clave no existe */
int64_t lg_dict_has(const LgDict* d, int64_t key_slot);
int64_t lg_dict_len(const LgDict* d);
/* Número de entradas, y acceso por posición de inserción (para iterar e imprimir). */
int64_t lg_dict_key_at(const LgDict* d, int64_t i);
int64_t lg_dict_val_at(const LgDict* d, int64_t i);

/* ---------- Tensores ---------- */
#define LG_DT_FLOAT 0  /* elementos double */
#define LG_DT_INT 1    /* elementos int64_t */

typedef struct LgTensor {
  LgHeader h;
  int64_t dtype;
  int64_t rank;
  int64_t* shape;     /* rank dimensiones */
  int64_t numel;
  void* data;         /* numel elementos del dtype, contiguos, orden row-major */
} LgTensor;

LgTensor* lg_tensor_zeros(int64_t dtype, int64_t rank, const int64_t* shape);
LgTensor* lg_tensor_ones(int64_t dtype, int64_t rank, const int64_t* shape);
LgTensor* lg_tensor_randn(int64_t dtype, int64_t rank, const int64_t* shape);
/* Desde una lista plana de floats/ints (slots). panic si len != producto de la forma. */
LgTensor* lg_tensor_from_list(int64_t dtype, LgList* values, int64_t rank, const int64_t* shape);
LgTensor* lg_tensor_matmul(const LgTensor* a, const LgTensor* b);  /* panic si formas incompatibles */
LgTensor* lg_tensor_add(const LgTensor* a, const LgTensor* b);     /* misma forma */
LgTensor* lg_tensor_sub(const LgTensor* a, const LgTensor* b);
LgTensor* lg_tensor_mul(const LgTensor* a, const LgTensor* b);     /* elemento a elemento */
LgTensor* lg_tensor_transpose(const LgTensor* a);                  /* rank 2 */
int64_t lg_tensor_rank(const LgTensor* t);
int64_t lg_tensor_dim(const LgTensor* t, int64_t i);
int64_t lg_tensor_numel(const LgTensor* t);
LgList* lg_tensor_shape_list(const LgTensor* t);                   /* List[int] nueva */
double lg_tensor_get_float(const LgTensor* t, int64_t flat_index);
int64_t lg_tensor_get_int(const LgTensor* t, int64_t flat_index);
/* Indexación t[i, j, ...] con n índices (1 <= n <= rank). indices: n valores int64.
 * Negativos cuentan desde el final de cada dimensión. file/line: ubicación para los errores de ejecución.
 * Índice fuera de rango -> "file:line: error de ejecución: índice X fuera de rango (dimensión D de tamaño S)".
 * index_float/index_int exigen n == rank (elemento escalar) y devuelven el valor (NO retienen nada). */
double lg_tensor_index_float(const LgTensor* t, const char* file, int64_t line, int64_t n, const int64_t* indices);
int64_t lg_tensor_index_int(const LgTensor* t, const char* file, int64_t line, int64_t n, const int64_t* indices);
/* Asignación t[i, j, ...] = v. Exige n == rank. Si el dtype no coincide convierte (truncando). */
void lg_tensor_set_float(LgTensor* t, const char* file, int64_t line, int64_t n, const int64_t* indices, double v);
void lg_tensor_set_int(LgTensor* t, const char* file, int64_t line, int64_t n, const int64_t* indices, int64_t v);
/* Índice parcial t[i, ...] con n < rank: devuelve un tensor NUEVO (copia) con la forma restante
 * shape[n..rank). Posesión +1 para el llamador. n == rank produce un tensor de rango 0. */
LgTensor* lg_tensor_slice(const LgTensor* t, const char* file, int64_t line, int64_t n, const int64_t* indices);
/* Operación tensor-escalar elemento a elemento, devuelve tensor nuevo de la misma forma.
 * op: 0 '+', 1 '-', 2 '*', 3 '/'. scalar_left = 0: t op s; scalar_left = 1: s op t.
 * División por escalar cero -> "división por cero". Enteros: desbordamiento -> error con file:line.
 * El dtype del tensor debe coincidir con la función (float -> lg_tensor_scale_float, int -> lg_tensor_scale_int). */
LgTensor* lg_tensor_scale_float(const LgTensor* t, double s, int64_t op, int64_t scalar_left, const char* file, int64_t line);
LgTensor* lg_tensor_scale_int(const LgTensor* t, int64_t s, int64_t op, int64_t scalar_left, const char* file, int64_t line);
void lg_tensor_check_shape(const LgTensor* t, int64_t rank, const int64_t* shape, const char* file, int64_t line);
void lg_tensor_print(const LgTensor* t);

/* ---------- Hilos (Hilo[T]) y canales (Canal[T]) ---------- */
/* Implementados en src/runtime/std/hilos.c (pthread; no hay soporte en Windows).
 * Los valores viajan como slots (ver arriba). Posesión:
 *   lg_hilo_lanzar: los slots de args se COPIAN. Los heap que contienen ya llevan +1 que el
 *     hilo libera al terminar (lo hace el envoltorio generado por el codegen).
 *   lg_hilo_esperar: devuelve el resultado con +1 (si es heap se retiene aquí).
 *   lg_hilo_destroy (al llegar rc a 0): espera al hilo si no se esperó y suelta el resultado. */
typedef struct LgHilo LgHilo;
typedef struct LgCanal LgCanal;
/* fn: envoltorio generado por el codegen. Recibe los slots de los argumentos y devuelve el slot del resultado. */
LgHilo* lg_hilo_lanzar(int64_t (*fn)(int64_t* args), const int64_t* args, int64_t nargs, int64_t result_is_heap);
int64_t lg_hilo_esperar(LgHilo* h);  /* slot del resultado, con +1 si es heap */
/* elem_is_heap: 1 si los slots son punteros con rc (el canal los libera si quedan sin recibir). */
LgCanal* lg_canal_nuevo(int64_t elem_is_heap);
/* Toma posesión del slot. Panic "file:line: error de ejecución: envío en un canal cerrado" si está cerrado. */
void lg_canal_enviar(LgCanal* c, int64_t slot, const char* file, int64_t line);
/* Bloquea hasta recibir un valor (*ok = 1) o hasta que el canal se cierre y se vacíe (*ok = 0). */
int64_t lg_canal_recibir(LgCanal* c, int64_t* ok);  /* la posesión pasa al llamador */
void lg_canal_cerrar(LgCanal* c);

/* ---------- Impresión ---------- */
void lg_print_int(int64_t v);
void lg_print_float(double v);
void lg_print_bool(int64_t v);
void lg_print_str(const LgStr* s);
void lg_print_text(const char* s);  /* texto literal, sin salto de línea */
void lg_print_space(void);
void lg_print_newline(void);

/* ---------- Entrada/salida y utilidades ---------- */
void lg_runtime_init(int argc, char** argv);  /* lo llama el main generado */

#ifdef __cplusplus
}
#endif

#endif /* LG_RT_H */
