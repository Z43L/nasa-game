/* Tests del runtime del lenguaje (lg_rt.c).
 * Compilar y ejecutar desde la raíz del proyecto:
 *   cc -std=c11 -Wall -Wextra -Isrc/runtime tests/runtime/runtime_test.c src/runtime/lg_rt.c -lm -o build_rt_test && ./build_rt_test
 * Los contadores de objetos vivos están en el propio test, no en la librería. */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lg_rt.h"

static int g_checks = 0;
static int g_fallos = 0;

#define CHECK(cond)                                                              \
  do {                                                                           \
    g_checks++;                                                                  \
    if (!(cond)) {                                                               \
      g_fallos++;                                                                \
      fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
    }                                                                            \
  } while (0)

/* ---------- Objetos de prueba con contador de vivos ---------- */

static int64_t g_vivos = 0;

typedef struct Probe {
  LgHeader h;
  int64_t id;
} Probe;

static void probe_destroy(void* p) {
  g_vivos--;
  free(p);
}

static Probe* probe_new(int64_t id) {
  Probe* p = lg_object_new(sizeof(Probe), probe_destroy);
  p->id = id;
  g_vivos++;
  return p;
}

/* ---------- Conversiones slot <-> valor ---------- */

static int64_t ptr_slot(const void* p) { return (int64_t)(intptr_t)p; }

static int64_t f2s(double d) {
  int64_t s;
  memcpy(&s, &d, sizeof s);
  return s;
}

static void check_str(const LgStr* s, const char* esperado) {
  size_t n = strlen(esperado);
  CHECK(s != NULL);
  if (!s) return;
  CHECK(s->len == (int64_t)n);
  CHECK(strcmp(s->data, esperado) == 0);
  if (strcmp(s->data, esperado) != 0)
    fprintf(stderr, "  obtenido '%s', esperado '%s'\n", s->data, esperado);
}

static void check_fstr(double v, const char* esperado) {
  LgStr* s = lg_str_from_float(v);
  check_str(s, esperado);
  lg_release(s);
}

/* ---------- Pruebas ---------- */

static void test_referencias(void) {
  lg_retain(NULL);   /* NULL-seguro */
  lg_release(NULL);  /* NULL-seguro */

  Probe* p = probe_new(7);
  CHECK(p->h.rc == 1);
  CHECK(g_vivos == 1);
  lg_retain(p);
  CHECK(p->h.rc == 2);
  lg_release(p);
  CHECK(p->h.rc == 1);
  CHECK(g_vivos == 1);
  lg_release(p);      /* rc llega a 0: destroy libera */
  CHECK(g_vivos == 0);
}

static void test_cadenas(void) {
  LgStr* a = lg_str_new("hola", 4);
  LgStr* b = lg_str_new(" mundo", 6);
  check_str(a, "hola");
  CHECK(lg_str_len(a) == 4);

  LgStr* c = lg_str_concat(a, b);
  check_str(c, "hola mundo");
  CHECK(lg_str_len(c) == 10);

  LgStr* d = lg_str_new("hola mundo", 10);
  CHECK(lg_str_eq(c, d) == 1);
  CHECK(lg_str_eq(c, a) == 0);
  CHECK(lg_str_eq(NULL, NULL) == 1);

  LgStr* vacia = lg_str_new("", 0);
  LgStr* con_vacia = lg_str_concat(a, vacia);
  check_str(con_vacia, "hola");

  lg_release(a);
  lg_release(b);
  lg_release(c);
  lg_release(d);
  lg_release(vacia);
  lg_release(con_vacia);

  LgStr* i = lg_str_from_int(-42);
  check_str(i, "-42");
  lg_release(i);

  LgStr* t = lg_str_from_bool(1);
  check_str(t, "true");
  lg_release(t);
  LgStr* f = lg_str_from_bool(0);
  check_str(f, "false");
  lg_release(f);

  /* Representación corta de floats */
  check_fstr(3.0, "3.0");
  check_fstr(0.1, "0.1");
  check_fstr(2.5, "2.5");
  check_fstr(1e300, "1e+300");
  check_fstr(-0.0, "-0.0");
  check_fstr(100.0, "100.0");
  check_fstr(1e16, "1e+16");
  check_fstr(0.0001, "0.0001");
  check_fstr(1e-5, "1e-05");
  check_fstr(1.0 / 3.0, "0.3333333333333333");
  check_fstr(-2.5, "-2.5");
  check_fstr(NAN, "nan");
  check_fstr(INFINITY, "inf");
  check_fstr(-INFINITY, "-inf");

  /* Conversiones desde cadena (los casos inválidos provocan panic y no se prueban aquí) */
  LgStr* n = lg_str_new("  -17 ", 6);
  CHECK(lg_str_to_int(n) == -17);
  lg_release(n);
  LgStr* fl = lg_str_new("2.5", 3);
  CHECK(lg_str_to_float(fl) == 2.5);
  lg_release(fl);
}

static void test_listas(void) {
  LgList* l = lg_list_new(0, 0);
  for (int64_t i = 0; i < 1000; i++) lg_list_append(l, i * 2);
  CHECK(lg_list_len(l) == 1000);
  CHECK(lg_list_get(l, 0) == 0);
  CHECK(lg_list_get(l, 999) == 1998);
  CHECK(lg_list_get(l, -1) == 1998);   /* negativos cuentan desde el final */
  CHECK(lg_list_get(l, -1000) == 0);
  lg_list_set(l, 5, 77);
  CHECK(lg_list_get(l, 5) == 77);
  lg_list_set(l, -2, 123);
  CHECK(lg_list_get(l, 998) == 123);
  CHECK(lg_list_pop(l) == 1998);
  CHECK(lg_list_len(l) == 999);
  lg_release(l);

  /* Lista de objetos heap: el destructor libera cada slot */
  LgList* h = lg_list_new(1, 2);
  lg_list_append(h, ptr_slot(probe_new(1)));
  lg_list_append(h, ptr_slot(probe_new(2)));
  CHECK(g_vivos == 2);
  /* set libera el valor anterior (probe 1) */
  lg_list_set(h, 0, ptr_slot(probe_new(3)));
  CHECK(g_vivos == 2);
  /* pop devuelve el slot con su propiedad: lo soltamos a mano */
  Probe* sacado = (Probe*)(intptr_t)lg_list_pop(h);
  CHECK(sacado->id == 2);
  CHECK(g_vivos == 2);
  lg_release(sacado);
  CHECK(g_vivos == 1);
  for (int i = 0; i < 100; i++) lg_list_append(h, ptr_slot(probe_new(i)));
  CHECK(g_vivos == 101);
  lg_release(h);
  CHECK(g_vivos == 0);

  /* Pop en lista no heap no toca nada */
  LgList* e = lg_list_new(0, 1);
  lg_list_append(e, 9);
  CHECK(lg_list_pop(e) == 9);
  CHECK(lg_list_len(e) == 0);
  lg_release(e);
}

static void test_diccionarios(void) {
  /* Claves INT, con rehash y orden de inserción */
  LgDict* d = lg_dict_new(LG_KEY_INT, 0);
  for (int64_t i = 0; i < 100; i++) lg_dict_set(d, i, i * i);
  CHECK(lg_dict_len(d) == 100);
  int64_t found = -1;
  CHECK(lg_dict_get(d, 10, &found) == 100 && found == 1);
  CHECK(lg_dict_get(d, 1000, &found) == 0 && found == 0);
  CHECK(lg_dict_has(d, 99) == 1);
  CHECK(lg_dict_has(d, 100) == 0);
  lg_dict_set(d, 10, -1);  /* sobrescritura: no cambia la longitud ni el orden */
  CHECK(lg_dict_len(d) == 100);
  CHECK(lg_dict_get_or_panic(d, 10) == -1);
  CHECK(lg_dict_key_at(d, 10) == 10);
  CHECK(lg_dict_val_at(d, 10) == -1);
  CHECK(lg_dict_key_at(d, 0) == 0);
  CHECK(lg_dict_val_at(d, 99) == 99 * 99);
  lg_release(d);  /* no hay heap: nada que liberar salvo la propia estructura */

  LgDict* orden = lg_dict_new(LG_KEY_INT, 0);
  lg_dict_set(orden, 5, 50);
  lg_dict_set(orden, 3, 30);
  lg_dict_set(orden, 9, 90);
  CHECK(lg_dict_key_at(orden, 0) == 5);
  CHECK(lg_dict_key_at(orden, 1) == 3);
  CHECK(lg_dict_key_at(orden, 2) == 9);
  lg_release(orden);

  /* Claves BOOL */
  LgDict* b = lg_dict_new(LG_KEY_BOOL, 0);
  lg_dict_set(b, 1, 10);
  lg_dict_set(b, 0, 20);
  CHECK(lg_dict_get_or_panic(b, 1) == 10);
  CHECK(lg_dict_get_or_panic(b, 0) == 20);
  lg_release(b);

  /* Claves STR por contenido; el diccionario retiene la clave */
  LgDict* s = lg_dict_new(LG_KEY_STR, 1);
  LgStr* k1 = lg_str_new("a", 1);
  Probe* v1 = probe_new(1);
  lg_dict_set(s, ptr_slot(k1), ptr_slot(v1));
  CHECK(k1->h.rc == 2);  /* nuestra referencia + la del diccionario */
  CHECK(g_vivos == 1);

  LgStr* k2 = lg_str_new("a", 1);  /* distinto objeto, mismo contenido */
  int64_t f = 0;
  Probe* got = (Probe*)(intptr_t)lg_dict_get(s, ptr_slot(k2), &f);
  CHECK(f == 1);
  CHECK(got == v1);
  CHECK(lg_dict_has(s, ptr_slot(k2)) == 1);
  LgStr* otra = lg_str_new("b", 1);
  CHECK(lg_dict_has(s, ptr_slot(otra)) == 0);
  lg_release(otra);

  /* Sobrescritura por contenido: libera el valor anterior; la clave original se conserva */
  Probe* v2 = probe_new(2);
  lg_dict_set(s, ptr_slot(k2), ptr_slot(v2));
  CHECK(k2->h.rc == 1);  /* el diccionario no retuvo esta clave repetida */
  CHECK(k1->h.rc == 2);
  CHECK(g_vivos == 1);   /* v1 se liberó, v2 vive */
  CHECK(lg_dict_len(s) == 1);
  CHECK(lg_dict_get_or_panic(s, ptr_slot(k1)) == ptr_slot(v2));

  lg_release(k1);  /* ahora solo la retiene el diccionario */
  CHECK(k1->h.rc == 1);
  lg_release(k2);
  lg_release(s);   /* libera la clave k1 y el valor v2 */
  CHECK(g_vivos == 0);

  /* Valores heap: el diccionario los libera al destruirse */
  LgDict* hv = lg_dict_new(LG_KEY_INT, 1);
  lg_dict_set(hv, 1, ptr_slot(probe_new(1)));
  lg_dict_set(hv, 2, ptr_slot(probe_new(2)));
  CHECK(g_vivos == 2);
  lg_release(hv);
  CHECK(g_vivos == 0);
}

static void test_tensores(void) {
  int64_t sh23[2] = {2, 3};
  LgTensor* z = lg_tensor_zeros(LG_DT_FLOAT, 2, sh23);
  CHECK(lg_tensor_rank(z) == 2);
  CHECK(lg_tensor_dim(z, 1) == 3);
  CHECK(lg_tensor_numel(z) == 6);
  CHECK(lg_tensor_get_float(z, 5) == 0.0);
  lg_release(z);

  LgTensor* o = lg_tensor_ones(LG_DT_INT, 2, sh23);
  CHECK(lg_tensor_get_int(o, 0) == 1);
  CHECK(lg_tensor_get_int(o, 5) == 1);
  lg_release(o);

  /* Escalar: rango 0, un elemento */
  LgTensor* sc = lg_tensor_zeros(LG_DT_FLOAT, 0, NULL);
  CHECK(lg_tensor_numel(sc) == 1);
  lg_release(sc);

  /* A = [[1,2,3],[4,5,6]] */
  LgList* la = lg_list_new(0, 6);
  for (int i = 1; i <= 6; i++) lg_list_append(la, f2s((double)i));
  LgTensor* A = lg_tensor_from_list(LG_DT_FLOAT, la, 2, sh23);
  CHECK(lg_tensor_get_float(A, 0) == 1.0);
  CHECK(lg_tensor_get_float(A, 5) == 6.0);
  lg_release(la);

  /* B = [[7,8],[9,10],[11,12]] */
  LgList* lb = lg_list_new(0, 6);
  for (int i = 7; i <= 12; i++) lg_list_append(lb, f2s((double)i));
  int64_t sh32[2] = {3, 2};
  LgTensor* B = lg_tensor_from_list(LG_DT_FLOAT, lb, 2, sh32);
  lg_release(lb);

  LgTensor* C = lg_tensor_matmul(A, B);  /* [[58,64],[139,154]] */
  CHECK(lg_tensor_get_float(C, 0) == 58.0);
  CHECK(lg_tensor_get_float(C, 1) == 64.0);
  CHECK(lg_tensor_get_float(C, 2) == 139.0);
  CHECK(lg_tensor_get_float(C, 3) == 154.0);
  int64_t shp_c[2] = {2, 2};
  lg_tensor_check_shape(C, 2, shp_c, __FILE__, __LINE__);  /* no debe hacer panic */
  lg_release(C);

  /* Matmul con enteros */
  LgList* ia = lg_list_new(0, 6);
  for (int64_t i = 1; i <= 6; i++) lg_list_append(ia, i);
  LgTensor* IA = lg_tensor_from_list(LG_DT_INT, ia, 2, sh23);
  lg_release(ia);
  LgList* ib = lg_list_new(0, 6);
  for (int64_t i = 7; i <= 12; i++) lg_list_append(ib, i);
  LgTensor* IB = lg_tensor_from_list(LG_DT_INT, ib, 2, sh32);
  lg_release(ib);
  LgTensor* IC = lg_tensor_matmul(IA, IB);
  CHECK(lg_tensor_get_int(IC, 0) == 58);
  CHECK(lg_tensor_get_int(IC, 3) == 154);
  lg_release(IC);
  lg_release(IA);
  lg_release(IB);

  /* Elemento a elemento */
  LgTensor* sum = lg_tensor_add(A, A);
  CHECK(lg_tensor_get_float(sum, 2) == 6.0);
  lg_release(sum);
  LgTensor* dif = lg_tensor_sub(A, A);
  CHECK(lg_tensor_get_float(dif, 4) == 0.0);
  lg_release(dif);
  LgTensor* prod = lg_tensor_mul(A, A);
  CHECK(lg_tensor_get_float(prod, 5) == 36.0);
  lg_release(prod);

  /* Transpuesta: A^T = [[1,4],[2,5],[3,6]] */
  LgTensor* At = lg_tensor_transpose(A);
  CHECK(lg_tensor_dim(At, 0) == 3);
  CHECK(lg_tensor_dim(At, 1) == 2);
  CHECK(lg_tensor_get_float(At, 1) == 4.0);
  CHECK(lg_tensor_get_float(At, 2) == 2.0);
  CHECK(lg_tensor_get_float(At, 5) == 6.0);
  lg_release(At);

  /* Forma como lista */
  LgList* shp = lg_tensor_shape_list(A);
  CHECK(lg_list_len(shp) == 2);
  CHECK(lg_list_get(shp, 0) == 2);
  CHECK(lg_list_get(shp, 1) == 3);
  lg_release(shp);

  /* Impresión: "Tensor[float, (2, 3)]" seguido de las filas */
  lg_tensor_print(A);

  lg_release(A);
  lg_release(B);

  /* randn: media ~0 y varianza ~1 sobre 4000 muestras; todos finitos */
  int64_t sh4000[1] = {4000};
  LgTensor* r = lg_tensor_randn(LG_DT_FLOAT, 1, sh4000);
  double media = 0.0, var = 0.0;
  int finitos = 1;
  for (int64_t i = 0; i < 4000; i++) {
    double x = lg_tensor_get_float(r, i);
    if (!isfinite(x)) finitos = 0;
    media += x;
  }
  media /= 4000.0;
  for (int64_t i = 0; i < 4000; i++) {
    double x = lg_tensor_get_float(r, i) - media;
    var += x * x;
  }
  var /= 4000.0;
  CHECK(finitos);
  CHECK(fabs(media) < 0.1);
  CHECK(fabs(var - 1.0) < 0.1);
  lg_release(r);
}

static void test_impresion(void) {
  /* Salida visible en stdout; aquí se comprueba que no hace panic y el formato base */
  lg_print_int(-5);
  lg_print_space();
  lg_print_float(3.0);
  lg_print_space();
  lg_print_bool(0);
  lg_print_space();
  LgStr* s = lg_str_new("texto", 5);
  lg_print_str(s);
  lg_release(s);
  lg_print_space();
  lg_print_text("literal");
  lg_print_newline();
}

int main(int argc, char** argv) {
  lg_runtime_init(argc, argv);

  test_referencias();
  test_cadenas();
  test_listas();
  test_diccionarios();
  test_tensores();
  test_impresion();

  CHECK(g_vivos == 0);
  fflush(stdout);
  if (g_fallos) {
    fprintf(stderr, "runtime_test: %d de %d comprobaciones fallaron\n", g_fallos, g_checks);
    return 1;
  }
  printf("runtime_test: OK (%d comprobaciones)\n", g_checks);
  return 0;
}
