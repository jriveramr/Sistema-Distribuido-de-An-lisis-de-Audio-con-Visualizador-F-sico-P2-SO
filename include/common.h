/*******************************************************************************
 * common.h — Estructuras y constantes compartidas entre Maestro y Trabajadores
 *
 * Sistema de Análisis de Audio Distribuido con OpenMPI
 * Arquitectura: 1 maestro + N trabajadores (mínimo 2 trabajadores)
 ******************************************************************************/

#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdlib.h>
#include <math.h>

/* ─── Rango MPI ─────────────────────────────────────────────────────────────*/
#define MASTER_RANK         0

/* ─── Tags de mensajes MPI (comunicación punto a punto) ─────────────────────*/
#define TAG_SEGMENT_META    10   /* Maestro → Trabajador: metadatos del segmento */
#define TAG_SEGMENT_DATA    11   /* Maestro → Trabajador: datos PCM cifrados     */
#define TAG_RESULT          12   /* Trabajador → Maestro: resultado de análisis   */
#define TAG_TERMINATE       99   /* Maestro → Trabajador: señal de fin            */

/* ─── Parámetros de audio ────────────────────────────────────────────────────*/
#define SAMPLE_RATE         44100   /* Hz (estándar CD)                          */
#define WINDOW_SIZE         2048    /* Muestras por ventana FFT (potencia de 2)  */
#define HOP_SIZE            1024    /* Desplazamiento entre ventanas             */

/* ─── Bandas de frecuencia (índices FFT) ─────────────────────────────────────*/
#define BAND_SUBBASS_LO     20      /* Hz */
#define BAND_SUBBASS_HI     250     /* Hz */
#define BAND_MID_LO         250     /* Hz */
#define BAND_MID_HI         2000    /* Hz */
#define BAND_UPPERMID_LO    2000    /* Hz */
#define BAND_UPPERMID_HI    6000    /* Hz */
#define BAND_HIGH_LO        6000    /* Hz */
#define BAND_HIGH_HI        20000   /* Hz */

/* ─── LED matrix — ACTUALIZADO a 7x7 ─────────────────────────────────────────*/
#define LED_COLS            7       /* 7 columnas: 4 bandas + RMS + (nuevas)    */
#define LED_ROWS            7       /* 7 filas: resolución aumentada            */

/* Mapeo de columnas para matriz 7x7: */
/*   Col 0: Sub-bass  (20-250 Hz)    */
/*   Col 1: Mid       (250-2000 Hz)  */
/*   Col 2: Upper-mid (2000-6000 Hz) */
/*   Col 3: High      (6000-20000 Hz) */
/*   Col 4: RMS       (amplitud global) */
/*   Col 5: BPM (opcional, intensidad rítmica) */
/*   Col 6: Clasificación (codificada como nivel) */

/* ─── Cifrado XOR ────────────────────────────────────────────────────────────*/
#define XOR_KEY_LEN         16
static const uint8_t XOR_KEY[XOR_KEY_LEN] = {
    0xA5, 0x3C, 0x7F, 0x91, 0x2B, 0xE4, 0x58, 0xD0,
    0x6E, 0xB3, 0x19, 0x47, 0xCC, 0x05, 0x88, 0xF2
};

/* ─── Clasificación de audio ─────────────────────────────────────────────────*/
typedef enum {
    CLASS_SILENCE = 0,
    CLASS_NOISE   = 1,
    CLASS_VOICE   = 2,
    CLASS_MUSIC   = 3
} AudioClass;

/* Definido en master.c; extern en los demás TUs que lo necesiten */
#ifdef AUDIO_DEFINE_CLASS_NAMES
const char *AudioClassName[] = { "SILENCIO", "RUIDO", "VOZ", "MÚSICA" };
#else
extern const char *AudioClassName[];
#endif

/* ─── Cabecera WAV (PCM 16-bit, little-endian) ───────────────────────────────*/
typedef struct __attribute__((packed)) {
    /* RIFF chunk */
    char     riff_id[4];        /* "RIFF"              */
    uint32_t riff_size;         /* Tamaño total - 8    */
    char     wave_id[4];        /* "WAVE"              */
    /* fmt chunk */
    char     fmt_id[4];         /* "fmt "              */
    uint32_t fmt_size;          /* 16 para PCM         */
    uint16_t audio_format;      /* 1 = PCM lineal      */
    uint16_t num_channels;      /* 1=mono, 2=stereo    */
    uint32_t sample_rate;       /* Hz                  */
    uint32_t byte_rate;         /* sample_rate * block_align */
    uint16_t block_align;       /* num_channels * bits/8     */
    uint16_t bits_per_sample;   /* 16                        */
} WavHeader;

/* ─── Metadatos del segmento (enviados antes de los datos) ───────────────────*/
typedef struct {
    int32_t  worker_id;         /* Rango del trabajador destino */
    uint32_t sample_rate;       /* Hz del archivo WAV           */
    uint32_t num_channels;      /* Canales del archivo          */
    uint32_t num_samples;       /* Muestras PCM en este segmento */
    uint32_t segment_index;     /* Número de segmento (0-based) */
    uint32_t total_segments;    /* Total de segmentos           */
} SegmentMeta;

/* ─── Resultado parcial de un trabajador ─────────────────────────────────────*/
typedef struct {
    int32_t  worker_id;
    uint32_t segment_index;
    double   dominant_freq;     /* Hz de la frecuencia dominante       */
    double   rms_amplitude;     /* Amplitud RMS normalizada [0,1]      */
    double   energy_subbass;    /* Energía relativa sub-bass [0,1]     */
    double   energy_mid;        /* Energía relativa mid [0,1]          */
    double   energy_uppermid;   /* Energía relativa upper-mid [0,1]    */
    double   energy_high;       /* Energía relativa high [0,1]         */
    double   bpm_estimate;      /* BPM estimado (0 si no detectable)   */
    uint8_t  spectrogram[LED_COLS]; /* Columnas para la matriz LED 7x7 */
} WorkerResult;

/* ─── Resultado global consolidado por el maestro ────────────────────────────*/
typedef struct {
    double      dominant_freq;
    double      rms_amplitude;
    double      energy_subbass;
    double      energy_mid;
    double      energy_uppermid;
    double      energy_high;
    double      bpm_estimate;
    AudioClass  classification;
    uint8_t     led_frame[LED_COLS];  /* Ahora 7 columnas */
} GlobalResult;

/* ─── Número de bytes PCM por muestra (16-bit mono o stereo) ─────────────────*/
static inline uint32_t bytes_per_sample(uint16_t channels) {
    return (uint32_t)channels * 2u;   /* 2 bytes por canal por muestra */
}

#endif /* COMMON_H */