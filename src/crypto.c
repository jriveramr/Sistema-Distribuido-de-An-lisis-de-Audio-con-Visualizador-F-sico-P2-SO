/*******************************************************************************
 * crypto.c — Implementación del cifrado XOR con clave rotativa
 ******************************************************************************/

#include <string.h>
#include <stdio.h>
#include "crypto.h"

/* ─── XOR in-place ─────────────────────────────────────────────────────────── */
void xor_crypt(uint8_t *buf, size_t len, size_t global_off)
{
    if (!buf || len == 0) return;

    for (size_t i = 0; i < len; ++i) {
        /* Índice en la clave: depende del offset global para coherencia
         * entre segmentos distintos enviados a trabajadores distintos. */
        size_t key_idx = (global_off + i) % XOR_KEY_LEN;
        buf[i] ^= XOR_KEY[key_idx];
    }
}

/* ─── XOR con copia ─────────────────────────────────────────────────────────── */
uint8_t *xor_crypt_alloc(const uint8_t *src, size_t len, size_t global_off)
{
    if (!src || len == 0) return NULL;

    uint8_t *dst = (uint8_t *)malloc(len);
    if (!dst) {
        fprintf(stderr, "[crypto] malloc(%zu) falló\n", len);
        return NULL;
    }

    memcpy(dst, src, len);
    xor_crypt(dst, len, global_off);
    return dst;
}