/* Nativos de stdlib/cadena.lg. Contratos en la cabecera de stdlib/cadena.lg. */
#include <string.h>

#include "lg_rt.h"

/* Posición de la primera aparición de `sub` en `s`, o -1. Una subcadena vacía se encuentra en 0. */
int64_t lg_std_cadena_buscar(const LgStr* s, const LgStr* sub) {
  if (!s || !sub) lg_panic_at(NULL, 0, "cadena.buscar: argumento nulo");
  if (sub->len == 0) return 0;
  if (sub->len > s->len) return -1;
  for (int64_t i = 0; i + sub->len <= s->len; i++) {
    if (memcmp(s->data + i, sub->data, (size_t)sub->len) == 0) return i;
  }
  return -1;
}

/* Subcadena de `s` desde `ini` (incluido) hasta `fin` (excluido). Los límites se ajustan al rango
 * válido: nunca hay panic por índices fuera de rango. */
LgStr* lg_std_cadena_sub(const LgStr* s, int64_t ini, int64_t fin) {
  if (!s) lg_panic_at(NULL, 0, "cadena.sub: argumento nulo");
  if (ini < 0) ini = 0;
  if (fin > s->len) fin = s->len;
  if (ini >= fin) return lg_str_new("", 0);
  return lg_str_new(s->data + ini, fin - ini);
}
