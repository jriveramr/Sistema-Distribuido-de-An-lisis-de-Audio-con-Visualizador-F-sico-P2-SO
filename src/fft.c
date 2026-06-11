/*******************************************************************************
 * fft.c — Implementación FFT Cooley–Tukey radix-2 DIT y análisis de audio
 ******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "fft.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ══════════════════════════════════════════════════════════════════════════════
 * Helpers internos
 * ══════════════════════════════════════════════════════════════════════════════*/

/* Verifica que N sea potencia de 2 */
static int is_power_of_two(size_t n)
{
    return (n > 0) && ((n & (n - 1)) == 0);
}

/* Reordena bit-reversal in-place */
static void bit_reverse(Complex *x, size_t N)
{
    size_t bits = 0;
    size_t tmp  = N >> 1;
    while (tmp) { ++bits; tmp >>= 1; }

    for (size_t i = 0; i < N; ++i) {
        size_t j = 0;
        size_t k = i;
        for (size_t b = 0; b < bits; ++b) {
            j = (j << 1) | (k & 1);
            k >>= 1;
        }
        if (j > i) {
            Complex t = x[i];
            x[i] = x[j];
            x[j] = t;
        }
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * FFT Cooley–Tukey radix-2 DIT in-place
 * ══════════════════════════════════════════════════════════════════════════════*/
int fft_radix2(Complex *x, size_t N, int inv)
{
    if (!x)                   { fprintf(stderr, "[fft] x es NULL\n");              return -1; }
    if (!is_power_of_two(N))  { fprintf(stderr, "[fft] N=%zu no es potencia de 2\n", N); return -1; }

    bit_reverse(x, N);

    /* Mariposas */
    for (size_t s = 2; s <= N; s <<= 1) {
        double angle = (inv ? 1.0 : -1.0) * 2.0 * M_PI / (double)s;
        Complex wm = { cos(angle), sin(angle) };

        for (size_t k = 0; k < N; k += s) {
            Complex w = { 1.0, 0.0 };
            for (size_t j = 0; j < s / 2; ++j) {
                /* t = w * x[k + j + s/2] */
                Complex t = {
                    w.re * x[k + j + s/2].re - w.im * x[k + j + s/2].im,
                    w.re * x[k + j + s/2].im + w.im * x[k + j + s/2].re
                };
                Complex u = x[k + j];

                x[k + j].re         = u.re + t.re;
                x[k + j].im         = u.im + t.im;
                x[k + j + s/2].re   = u.re - t.re;
                x[k + j + s/2].im   = u.im - t.im;

                /* Actualizar twiddle factor */
                double new_re = w.re * wm.re - w.im * wm.im;
                double new_im = w.re * wm.im + w.im * wm.re;
                w.re = new_re;
                w.im = new_im;
            }
        }
    }

    /* Normalización para IFFT */
    if (inv) {
        double scale = 1.0 / (double)N;
        for (size_t i = 0; i < N; ++i) {
            x[i].re *= scale;
            x[i].im *= scale;
        }
    }

    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Ventana de Hann
 * ══════════════════════════════════════════════════════════════════════════════*/
static void apply_hann(Complex *x, size_t N)
{
    for (size_t i = 0; i < N; ++i) {
        double w = 0.5 * (1.0 - cos(2.0 * M_PI * (double)i / (double)(N - 1)));
        x[i].re *= w;
        x[i].im *= w;
    }
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Conversión frecuencia → índice FFT
 * ══════════════════════════════════════════════════════════════════════════════*/
static size_t freq_to_bin(double freq, uint32_t sample_rate, size_t N)
{
    size_t bin = (size_t)(freq * (double)N / (double)sample_rate);
    if (bin >= N / 2) bin = N / 2 - 1;
    return bin;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Análisis de una ventana PCM 16-bit
 * ══════════════════════════════════════════════════════════════════════════════*/
int analyze_window(const int16_t *pcm, size_t n_samples,
                   uint32_t sample_rate, WorkerResult *out)
{
    if (!pcm || !out || n_samples == 0 || sample_rate == 0) {
        fprintf(stderr, "[fft] analyze_window: argumento inválido\n");
        return -1;
    }

    /* Usar WINDOW_SIZE puntos; si la ventana es menor se hace zero-padding */
    size_t N = WINDOW_SIZE;
    Complex *buf = (Complex *)calloc(N, sizeof(Complex));
    if (!buf) {
        fprintf(stderr, "[fft] calloc(%zu) falló\n", N);
        return -1;
    }

    size_t use = (n_samples < N) ? n_samples : N;

    /* Copiar muestras normalizadas a [-1.0, 1.0] y calcular RMS simultáneamente */
    double rms_sum = 0.0;
    for (size_t i = 0; i < use; ++i) {
        double s = (double)pcm[i] / 32768.0;
        buf[i].re = s;
        buf[i].im = 0.0;
        rms_sum  += s * s;
    }

    out->rms_amplitude = sqrt(rms_sum / (double)use);

    /* Ventana de Hann antes de la FFT */
    apply_hann(buf, N);

    /* FFT directa */
    if (fft_radix2(buf, N, 0) != 0) {
        free(buf);
        return -1;
    }

    /* ── Magnitudes (solo mitad positiva del espectro) ─────────────────────── */
    size_t half = N / 2;
    double *mag = (double *)malloc(half * sizeof(double));
    if (!mag) {
        fprintf(stderr, "[fft] malloc mag falló\n");
        free(buf);
        return -1;
    }

    double total_energy = 0.0;
    size_t peak_bin     = 0;
    double peak_mag     = 0.0;

    for (size_t i = 0; i < half; ++i) {
        mag[i]        = sqrt(buf[i].re * buf[i].re + buf[i].im * buf[i].im);
        total_energy += mag[i] * mag[i];
        if (mag[i] > peak_mag) {
            peak_mag = mag[i];
            peak_bin = i;
        }
    }

    /* ── Frecuencia dominante ────────────────────────────────────────────────── */
    out->dominant_freq = (total_energy < 1e-12) ? 0.0 :
        (double)peak_bin * (double)sample_rate / (double)N;

    /* ── Energía por bandas ──────────────────────────────────────────────────── */
    size_t b_sb_lo = freq_to_bin(BAND_SUBBASS_LO,   sample_rate, N);
    size_t b_sb_hi = freq_to_bin(BAND_SUBBASS_HI,   sample_rate, N);
    size_t b_m_lo  = freq_to_bin(BAND_MID_LO,       sample_rate, N);
    size_t b_m_hi  = freq_to_bin(BAND_MID_HI,       sample_rate, N);
    size_t b_um_lo = freq_to_bin(BAND_UPPERMID_LO,  sample_rate, N);
    size_t b_um_hi = freq_to_bin(BAND_UPPERMID_HI,  sample_rate, N);
    size_t b_h_lo  = freq_to_bin(BAND_HIGH_LO,      sample_rate, N);
    size_t b_h_hi  = freq_to_bin(BAND_HIGH_HI,      sample_rate, N);

    double e_sb = 0.0, e_m = 0.0, e_um = 0.0, e_h = 0.0;

    for (size_t i = b_sb_lo; i <= b_sb_hi && i < half; ++i) e_sb += mag[i] * mag[i];
    for (size_t i = b_m_lo;  i <= b_m_hi  && i < half; ++i) e_m  += mag[i] * mag[i];
    for (size_t i = b_um_lo; i <= b_um_hi && i < half; ++i) e_um += mag[i] * mag[i];
    for (size_t i = b_h_lo;  i <= b_h_hi  && i < half; ++i) e_h  += mag[i] * mag[i];

    /* Normalizar respecto a la energía total */
    if (total_energy > 1e-12) {
        out->energy_subbass  = e_sb  / total_energy;
        out->energy_mid      = e_m   / total_energy;
        out->energy_uppermid = e_um  / total_energy;
        out->energy_high     = e_h   / total_energy;
    } else {
        out->energy_subbass  = 0.0;
        out->energy_mid      = 0.0;
        out->energy_uppermid = 0.0;
        out->energy_high     = 0.0;
    }

    /* Frame LED de la ventana individual (será sobreescrito por el maestro) */
    build_led_frame(out->energy_subbass, out->energy_mid,
                    out->energy_uppermid, out->energy_high,
                    out->rms_amplitude, out->spectrogram);

    free(mag);
    free(buf);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Estimación de BPM por detección de picos de energía (onset detection)
 * ══════════════════════════════════════════════════════════════════════════════*/
double compute_bpm(const double *energies, size_t n_windows,
                   uint32_t hop_size, uint32_t sample_rate)
{
    if (!energies || n_windows < 4 || sample_rate == 0) return 0.0;

    /* Duración de cada ventana en segundos */
    double hop_sec = (double)hop_size / (double)sample_rate;

    /* Calcular diferencia de energía (onset strength) */
    double *onset = (double *)calloc(n_windows, sizeof(double));
    if (!onset) return 0.0;

    double mean_e = 0.0;
    for (size_t i = 0; i < n_windows; ++i) mean_e += energies[i];
    mean_e /= (double)n_windows;

    for (size_t i = 1; i < n_windows; ++i) {
        double diff = energies[i] - energies[i - 1];
        onset[i] = (diff > 0.0) ? diff : 0.0;
    }

    /* Umbral adaptativo: media + 1 desviación estándar */
    double sum = 0.0, sum2 = 0.0;
    for (size_t i = 0; i < n_windows; ++i) { sum += onset[i]; sum2 += onset[i] * onset[i]; }
    double mean_o  = sum  / (double)n_windows;
    double var_o   = sum2 / (double)n_windows - mean_o * mean_o;
    double std_o   = (var_o > 0.0) ? sqrt(var_o) : 0.0;
    double thresh  = mean_o + std_o;

    /* Detectar picos (ventana de supresión = 0.2 s) */
    int    suppress = (int)(0.2 / hop_sec);
    if (suppress < 1) suppress = 1;

    size_t *peaks    = (size_t *)malloc(n_windows * sizeof(size_t));
    size_t  n_peaks  = 0;
    int     cooldown = 0;

    if (!peaks) {
        fprintf(stderr, "[fft] malloc peaks(%zu) falló\n",
                n_windows * sizeof(size_t));
        free(onset);
        return 0.0;
    }

    for (size_t i = 1; i + 1 < n_windows; ++i) {
        if (cooldown > 0) { --cooldown; continue; }
        if (onset[i] > thresh &&
            onset[i] > onset[i - 1] &&
            onset[i] > onset[i + 1]) {
            peaks[n_peaks++] = i;
            cooldown = suppress;
        }
    }

    double bpm = 0.0;
    if (n_peaks >= 2) {
        /* Intervalo medio entre picos consecutivos → BPM */
        double total_interval = 0.0;
        for (size_t i = 1; i < n_peaks; ++i)
            total_interval += (double)(peaks[i] - peaks[i - 1]) * hop_sec;
        double mean_interval = total_interval / (double)(n_peaks - 1);
        bpm = (mean_interval > 0.0) ? (60.0 / mean_interval) : 0.0;

        /* Filtro plausibilidad BPM humano (40–220 BPM) */
        if (bpm < 40.0 || bpm > 220.0) bpm = 0.0;
    }

    free(onset);
    free(peaks);
    return bpm;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Construcción del frame LED (5 columnas × 5 filas)
 *
 * Columnas:  0=Sub-bass | 1=Mid | 2=Upper-mid | 3=High | 4=RMS
 * Filas:     bit 0 = fila baja ... bit 4 = fila alta
 * ══════════════════════════════════════════════════════════════════════════════*/
void build_led_frame(double energy_subbass, double energy_mid,
                     double energy_uppermid, double energy_high,
                     double rms_amplitude,
                     uint8_t frame[LED_COLS])
{
    double values[LED_COLS] = {
        energy_subbass,
        energy_mid,
        energy_uppermid,
        energy_high,
        rms_amplitude
    };

    for (int col = 0; col < LED_COLS; ++col) {
        double v = values[col];
        if (v < 0.0) v = 0.0;
        if (v > 1.0) v = 1.0;

        /* Cuántas filas encender (0–5) */
        int rows_on = (int)(v * (double)LED_ROWS + 0.5);
        if (rows_on > LED_ROWS) rows_on = LED_ROWS;

        uint8_t byte = 0;
        for (int r = 0; r < rows_on; ++r)
            byte |= (uint8_t)(1u << r);

        frame[col] = byte;
    }
}