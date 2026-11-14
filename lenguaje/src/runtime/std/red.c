/* Nativos de stdlib/red.lg. Contratos en la cabecera de stdlib/red.lg.
 * Son independientes de la forma del tensor: operan sobre `numel` elementos contiguos. */
#include <math.h>
#include <stdint.h>

#include "lg_rt.h"

/* exp(x) elemento a elemento. Devuelve un tensor nuevo de la misma forma (propiedad del llamador).
 * Solo admite Tensor[float]. */
LgTensor* lg_std_exp_tensor(const LgTensor* t) {
  if (!t) lg_panic_at(NULL, 0, "exp: tensor nulo");
  if (t->dtype != LG_DT_FLOAT) lg_panic_at(NULL, 0, "exp: se esperaba Tensor[float]");
  LgTensor* r = lg_tensor_zeros(LG_DT_FLOAT, t->rank, t->shape);
  const double* src = (const double*)t->data;
  double* dst = (double*)r->data;
  for (int64_t i = 0; i < t->numel; i++) dst[i] = exp(src[i]);
  return r;
}

/* max(x, s) elemento a elemento. Devuelve un tensor nuevo de la misma forma (propiedad del llamador).
 * Solo admite Tensor[float]. */
LgTensor* lg_std_max_tensor(const LgTensor* t, double s) {
  if (!t) lg_panic_at(NULL, 0, "max: tensor nulo");
  if (t->dtype != LG_DT_FLOAT) lg_panic_at(NULL, 0, "max: se esperaba Tensor[float]");
  LgTensor* r = lg_tensor_zeros(LG_DT_FLOAT, t->rank, t->shape);
  const double* src = (const double*)t->data;
  double* dst = (double*)r->data;
  for (int64_t i = 0; i < t->numel; i++) dst[i] = src[i] > s ? src[i] : s;
  return r;
}
