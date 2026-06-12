/*******************************************************************************
 * fft.h — FFT Cooley–Tukey radix-2 DIT y análisis espectral
 ******************************************************************************/

#ifndef FFT_H
#define FFT_H

#include <stdint.h>
#include <stdlib.h>
#include "common.h"

/* ─── Número complejo de doble precisión ──────────────────────────────────── */
typedef struct {
    double re;
    double im;
} Complex;

/*
 * fft_radix2 — FFT in-place Cooley–Tukey radix-2 DIT.
 *
 * @param x   Array de N números complejos (entrada/salida).
 * @param N   Número de puntos. DEBE ser potencia de 2.
 * @param inv Si != 0 calcula la IFFT.
 * Retorna 0 en éxito, -1 si N no es potencia de 2 o x es NULL.
 */
int fft_radix2(Complex *x, size_t N, int inv);

/*
 * analyze_window — Analiza una ventana PCM 16-bit y rellena un WorkerResult.
 *
 * @param pcm         Muestras PCM (16-bit con signo, monoaural)
 * @param n_samples   Número de muestras (idealmente WINDOW_SIZE)
 * @param sample_rate Tasa de muestreo en Hz
 * @param out         Resultado de salida
 * Retorna 0 en éxito, -1 en error.
 */
int analyze_window(const int16_t *pcm, size_t n_samples,
                   uint32_t sample_rate, WorkerResult *out);

/*
 * compute_bpm — Estima BPM a partir de un vector de energías de ventana.
 *
 * @param energies    Array de energías por ventana
 * @param n_windows   Número de ventanas
 * @param hop_size    Muestras entre ventanas
 * @param sample_rate Hz
 * Retorna BPM estimado o 0.0 si no se puede determinar.
 */
double compute_bpm(const double *energies, size_t n_windows,
                   uint32_t hop_size, uint32_t sample_rate);

/*
 * build_led_frame — Genera el frame de 5 bytes para la matriz LED.
 *
 * Mapea las 4 bandas de energía + amplitud a las 5 columnas del display.
 * Cada byte representa una columna (bits 0-4 = filas 0-4).
 *
 * @param result  WorkerResult (o GlobalResult) con energías
 * @param frame   Array de 5 bytes de salida
 */
void build_led_frame(double energy_subbass, double energy_mid,
                     double energy_uppermid, double energy_high,
                     double rms_amplitude,
                     uint8_t frame[LED_COLS]);

#endif /* FFT_H */