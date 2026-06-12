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
 * build_led_frame — Genera el frame de 7 bytes para la matriz LED 7x7.
 *
 * Mapea:
 *   Col0 = sub-bass, Col1 = mid, Col2 = upper-mid, Col3 = high,
 *   Col4 = RMS, Col5 = BPM (normalizado), Col6 = clasificación
 *
 * @param energy_subbass   Energía sub-bass [0,1]
 * @param energy_mid       Energía mid [0,1]
 * @param energy_uppermid  Energía upper-mid [0,1]
 * @param energy_high      Energía high [0,1]
 * @param rms_amplitude    Amplitud RMS [0,1]
 * @param bpm_norm         BPM normalizado [0,1]
 * @param class_value      Valor de clasificación [0-3] -> normalizado
 * @param frame            Array de 7 bytes de salida
 */
void build_led_frame_ext(double energy_subbass, double energy_mid,
                         double energy_uppermid, double energy_high,
                         double rms_amplitude, double bpm_norm,
                         double class_value, uint8_t frame[LED_COLS]);

/* Versión simplificada (sin BPM y class) para compatibilidad */
static inline void build_led_frame(double energy_subbass, double energy_mid,
                                   double energy_uppermid, double energy_high,
                                   double rms_amplitude,
                                   uint8_t frame[LED_COLS]) {
    build_led_frame_ext(energy_subbass, energy_mid, energy_uppermid,
                        energy_high, rms_amplitude, 0.0, 0.0, frame);
}

#endif /* FFT_H */