/*******************************************************************************
 * crypto.h — Cifrado/descifrado XOR con clave rotativa
 ******************************************************************************/

#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdint.h>
#include <stdlib.h>
#include "common.h"

/*
 * xor_crypt — Cifra o descifra un buffer con XOR de clave rotativa.
 *
 * La operación es simétrica: cifrar(cifrar(x)) == x.
 * La clave rota en función del índice GLOBAL del byte dentro del archivo
 * para que segmentos distintos no rompan la alineación de la clave.
 *
 * @param buf         Buffer de entrada/salida (in-place)
 * @param len         Longitud en bytes
 * @param global_off  Desplazamiento global del primer byte del buffer
 *                    dentro del stream de datos original.
 */
void xor_crypt(uint8_t *buf, size_t len, size_t global_off);

/*
 * xor_crypt_alloc — Igual que xor_crypt pero devuelve un buffer nuevo.
 *                   El llamante es responsable de free().
 * Retorna NULL en caso de error de asignación.
 */
uint8_t *xor_crypt_alloc(const uint8_t *src, size_t len, size_t global_off);

#endif /* CRYPTO_H */