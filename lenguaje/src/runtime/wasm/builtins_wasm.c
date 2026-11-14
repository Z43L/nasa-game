/* Funciones de compiler-rt que LLVM llama para wasm32 y que no están en wasi-libc.
 * Solo se enlaza con lgc --target wasm32 (lo archiva CMake en liblg_runtime_wasm32.a).
 * La multiplicación de 128 bits (__multi3) aparece al comprobar el desbordamiento de int
 * (smul.with.overflow sobre i64). Se implementa con productos de 64 bits: no usa a su vez
 * multiplicación de 128 bits (eso sería recursivo). */
#include <stdint.h>
#include "lg_rt.h"

typedef __int128 ti_int;
typedef unsigned __int128 tu_int;

/* Producto completo de dos enteros sin signo de 64 bits, en 128 bits. */
static tu_int mul_u64_full(uint64_t a, uint64_t b) {
  uint64_t a0 = (uint32_t)a, a1 = a >> 32;
  uint64_t b0 = (uint32_t)b, b1 = b >> 32;
  uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
  uint64_t mid = (p00 >> 32) + (uint32_t)p01 + (uint32_t)p10;
  uint64_t lo = (mid << 32) | (uint32_t)p00;
  uint64_t hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
  return ((tu_int)hi << 64) | lo;
}

/* a * b modulo 2^128 (complemento a dos: el resultado con signo es el mismo bit a bit). */
ti_int __multi3(ti_int a, ti_int b) {
  uint64_t alo = (uint64_t)(tu_int)a, ahi = (uint64_t)((tu_int)a >> 64);
  uint64_t blo = (uint64_t)(tu_int)b, bhi = (uint64_t)((tu_int)b >> 64);
  tu_int r = mul_u64_full(alo, blo);
  uint64_t hi = (uint64_t)(r >> 64) + alo * bhi + ahi * blo;
  return (ti_int)(((tu_int)hi << 64) | (uint64_t)r);
}

/* Producto con signo de 128 bits que marca desbordamiento (lo llama wasi-libc, p. ej. en
 * clock_nanosleep). Trabaja con magnitudes: sin división ni multiplicación de 128 bits. */
ti_int __muloti4(ti_int a, ti_int b, int* overflow) {
  int neg = (a < 0) != (b < 0);
  tu_int ua = a < 0 ? (tu_int)0 - (tu_int)a : (tu_int)a;
  tu_int ub = b < 0 ? (tu_int)0 - (tu_int)b : (tu_int)b;
  uint64_t al = (uint64_t)ua, ah = (uint64_t)(ua >> 64);
  uint64_t bl = (uint64_t)ub, bh = (uint64_t)(ub >> 64);
  int ovf = ah != 0 && bh != 0;
  tu_int c1 = mul_u64_full(ah, bl), c2 = mul_u64_full(al, bh);
  if ((c1 >> 64) != 0 || (c2 >> 64) != 0) ovf = 1;
  tu_int base = mul_u64_full(al, bl);
  /* Suma de la parte alta: (c1 + c2) << 64 más base; un acarreo fuera de 128 bits es desbordamiento. */
  tu_int mid = c1 << 64;  /* c1 y c2 caben en 64 bits (comprobado arriba) */
  tu_int mid2 = c2 << 64;
  tu_int s1 = mid + mid2;
  if (s1 < mid) ovf = 1;
  tu_int total = base + s1;
  if (total < base) ovf = 1;
  /* Rango: positivo <= 2^127 - 1; negativo >= -2^127. */
  if (!neg && total > (tu_int)(((tu_int)1 << 127) - 1)) ovf = 1;
  if (neg && total > ((tu_int)1 << 127)) ovf = 1;
  if (overflow) *overflow = ovf;
  return neg ? (ti_int)((tu_int)0 - total) : (ti_int)total;
}

/* ---------- Cadenas devueltas a JavaScript (contrato en tests/wasm_ui y lib/ui/puente.js) ----------
 * Una función exportada con retorno str devuelve un "handle" (puntero a LgStr en memoria lineal).
 * El runtime guarda el último str devuelto en un único slot: lg_wasm_ret_store toma la posesión
 * (+1 de la función), libera el str anterior y devuelve el nuevo. Así el texto sigue vivo hasta la
 * SIGUIENTE exportación que devuelva str o hasta lg_wasm_str_liberar. JS debe leer ptr/len antes. */
static LgStr* lg_wasm_ultimo = NULL;

LgStr* lg_wasm_ret_store(LgStr* s) {
  LgStr* viejo = lg_wasm_ultimo;
  lg_wasm_ultimo = s;
  lg_release(viejo);  /* NULL-seguro; si s == viejo, la referencia nueva sustituye a la vieja */
  return s;
}

/* Exports (sin prefijo de usuario): el enlazador los exporta por export_name. */
__attribute__((export_name("lg_wasm_str_ptr")))
uint32_t lg_wasm_str_ptr(const LgStr* s) {
  return (uint32_t)(uintptr_t)(s ? s->data : "");
}

__attribute__((export_name("lg_wasm_str_len")))
int64_t lg_wasm_str_len(const LgStr* s) {
  return s ? s->len : 0;
}

__attribute__((export_name("lg_wasm_str_liberar")))
void lg_wasm_str_liberar(void) {
  LgStr* viejo = lg_wasm_ultimo;
  lg_wasm_ultimo = NULL;
  lg_release(viejo);
}
