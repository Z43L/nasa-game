/* Nativos de juego/src/codec.lg: base64 sobre bytes de cadenas lg (para guardar estado en cookies).
 * Alfabeto estándar (A-Z a-z 0-9 + /), seguro en valores de cookie. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lg_rt.h"

static const char TAB[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

LgStr* lg_std_codec_base64_codificar(const LgStr* s) {
  int64_t n = s->len;
  int64_t cap = ((n + 2) / 3) * 4 + 1;
  char* out = (char*)malloc((size_t)cap);
  if (!out) lg_panic_at(NULL, 0, "memoria agotada");
  int64_t j = 0;
  for (int64_t i = 0; i < n; i += 3) {
    uint32_t b0 = (unsigned char)s->data[i];
    uint32_t b1 = i + 1 < n ? (unsigned char)s->data[i + 1] : 0;
    uint32_t b2 = i + 2 < n ? (unsigned char)s->data[i + 2] : 0;
    uint32_t t = (b0 << 16) | (b1 << 8) | b2;
    out[j++] = TAB[(t >> 18) & 63];
    out[j++] = TAB[(t >> 12) & 63];
    out[j++] = i + 1 < n ? TAB[(t >> 6) & 63] : '=';
    out[j++] = i + 2 < n ? TAB[t & 63] : '=';
  }
  LgStr* r = lg_str_new(out, j);
  free(out);
  return r;
}

/* Devuelve "" si la entrada no es base64 válido (la cookie se ignora). */
LgStr* lg_std_codec_base64_decodificar(const LgStr* s) {
  int64_t n = s->len;
  if (n % 4 != 0) return lg_str_new("", 0);
  char* out = (char*)malloc((size_t)(n / 4 * 3 + 1));
  if (!out) lg_panic_at(NULL, 0, "memoria agotada");
  int64_t j = 0;
  for (int64_t i = 0; i < n; i += 4) {
    uint32_t v[4];
    int pad = 0;
    for (int k = 0; k < 4; k++) {
      char c = s->data[i + k];
      if (c == '=') {
        if (i + 4 != n || k < 2) { free(out); return lg_str_new("", 0); }
        pad++;
        v[k] = 0;
        continue;
      }
      const char* p = c ? strchr(TAB, c) : NULL;
      if (!p) { free(out); return lg_str_new("", 0); }
      v[k] = (uint32_t)(p - TAB);
    }
    uint32_t t = (v[0] << 18) | (v[1] << 12) | (v[2] << 6) | v[3];
    out[j++] = (char)((t >> 16) & 255);
    if (pad < 2) out[j++] = (char)((t >> 8) & 255);
    if (pad < 1) out[j++] = (char)(t & 255);
  }
  LgStr* r = lg_str_new(out, j);
  free(out);
  return r;
}
