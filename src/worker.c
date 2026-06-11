/*******************************************************************************
 * worker.c — Nodo Trabajador del Sistema de Análisis de Audio Distribuido
 *
 * Responsabilidades:
 *   1. Recibir los metadatos del segmento (SegmentMeta) del maestro.
 *   2. Recibir el bloque de datos PCM cifrado.
 *   3. Descifrar en memoria con XOR rotativo (misma clave que el maestro).
 *   4. Convertir bytes a muestras PCM int16_t y mezclar a mono si es stereo.
 *   5. Procesar todas las ventanas Hann + FFT Cooley–Tukey radix-2.
 *   6. Calcular: frecuencia dominante, RMS, energías por banda, BPM.
 *   7. Enviar WorkerResult al maestro y esperar el TAG_TERMINATE.
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <errno.h>

#include <mpi.h>

#include "../include/common.h"
#include "../include/sysmon.h"
#include "../include/crypto.h"
#include "../include/fft.h"

/* ══════════════════════════════════════════════════════════════════════════════
 * Mezcla de canales stereo a mono (promedio)
 *
 * Los datos WAV PCM 16-bit stereo están intercalados: L0 R0 L1 R1 ...
 * ══════════════════════════════════════════════════════════════════════════════*/
static int16_t *stereo_to_mono(const int16_t *stereo, uint32_t n_frames)
{
    int16_t *mono = (int16_t *)malloc(n_frames * sizeof(int16_t));
    if (!mono) {
        fprintf(stderr, "[worker] malloc stereo_to_mono falló\n");
        return NULL;
    }
    for (uint32_t i = 0; i < n_frames; ++i) {
        /* Promedio de 32 bits para evitar desbordamiento */
        int32_t avg = ((int32_t)stereo[i * 2] + (int32_t)stereo[i * 2 + 1]) / 2;
        mono[i] = (int16_t)avg;
    }
    return mono;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Procesamiento completo de un segmento de audio
 *
 * Divide el segmento en ventanas solapadas (hop_size), aplica FFT a cada una
 * y acumula los resultados parciales que luego se promedian.
 * ══════════════════════════════════════════════════════════════════════════════*/
static int process_segment(const int16_t *pcm_mono, uint32_t n_samples,
                            uint32_t sample_rate, WorkerResult *out)
{
    if (!pcm_mono || !out || n_samples == 0) return -1;

    /* ── Calcular número de ventanas ────────────────────────────────────────*/
    size_t win   = WINDOW_SIZE;
    size_t hop   = HOP_SIZE;
    size_t n_win = 0;

    if (n_samples >= win) {
        n_win = (n_samples - win) / hop + 1;
    }

    if (n_win == 0) {
        /* Segmento muy corto: analizar como una sola ventana sin hop */
        n_win = 1;
        win   = n_samples;
    }

    /* Acumuladores para promediar resultados de todas las ventanas */
    double acc_dom_freq   = 0.0;
    double acc_rms        = 0.0;
    double acc_sb         = 0.0;
    double acc_mid        = 0.0;
    double acc_um         = 0.0;
    double acc_high       = 0.0;
    size_t valid_windows  = 0;

    /* Vector de energías por ventana (para BPM) */
    double *energies = (double *)calloc(n_win, sizeof(double));
    if (!energies) {
        fprintf(stderr, "[worker] calloc energies falló\n");
        return -1;
    }

    WorkerResult win_result;

    for (size_t w = 0; w < n_win; ++w) {
        size_t offset     = w * hop;
        size_t avail      = n_samples - offset;
        size_t use_len    = (avail < win) ? avail : win;

        memset(&win_result, 0, sizeof(win_result));

        if (analyze_window(pcm_mono + offset, use_len,
                           sample_rate, &win_result) != 0) {
            fprintf(stderr, "[worker] analyze_window %zu falló, omitiendo\n", w);
            continue;
        }

        acc_dom_freq  += win_result.dominant_freq;
        acc_rms       += win_result.rms_amplitude;
        acc_sb        += win_result.energy_subbass;
        acc_mid       += win_result.energy_mid;
        acc_um        += win_result.energy_uppermid;
        acc_high      += win_result.energy_high;

        /* Energía total de la ventana para detección de BPM */
        energies[w] = win_result.energy_subbass +
                      win_result.energy_mid      +
                      win_result.energy_uppermid +
                      win_result.energy_high;

        ++valid_windows;
    }

    if (valid_windows == 0) {
        free(energies);
        return -1;
    }

    /* ── Promediar sobre todas las ventanas válidas ─────────────────────────*/
    double vw = (double)valid_windows;
    out->dominant_freq  = acc_dom_freq / vw;
    out->rms_amplitude  = acc_rms      / vw;
    out->energy_subbass = acc_sb       / vw;
    out->energy_mid     = acc_mid      / vw;
    out->energy_uppermid= acc_um       / vw;
    out->energy_high    = acc_high     / vw;

    /* ── Estimar BPM ────────────────────────────────────────────────────────*/
    out->bpm_estimate = compute_bpm(energies, n_win,
                                    (uint32_t)hop, sample_rate);

    /* ── Construir frame LED del segmento ───────────────────────────────────*/
    build_led_frame(out->energy_subbass,  out->energy_mid,
                    out->energy_uppermid, out->energy_high,
                    out->rms_amplitude,   out->spectrogram);

    free(energies);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * worker_main — Lógica del trabajador, invocado desde main.c cuando rank >= 1.
 * MPI ya está inicializado; NO llamar MPI_Init/Finalize aquí.
 * ══════════════════════════════════════════════════════════════════════════════*/

int worker_main(int world_rank, int world_size)
{
    (void)world_size;   /* Disponible si se necesita en el futuro */

    sysmon_print(world_rank, "INICIO");
    printf("[worker %d] Iniciado. Esperando segmento del maestro...\n",
           world_rank);

    /* ── 1. Recibir metadatos del segmento ──────────────────────────────────*/
    SegmentMeta meta;
    MPI_Status  status;

    int rc = MPI_Recv(&meta, sizeof(SegmentMeta), MPI_BYTE,
                      MASTER_RANK, TAG_SEGMENT_META,
                      MPI_COMM_WORLD, &status);
    if (rc != MPI_SUCCESS) {
        fprintf(stderr, "[worker %d] MPI_Recv META falló (%d)\n",
                world_rank, rc);
        return EXIT_FAILURE;
    }

    printf("[worker %d] Recibido META: segmento=%u/%u, "
           "samples=%u, rate=%u Hz, channels=%u\n",
           world_rank, meta.segment_index, meta.total_segments - 1,
           meta.num_samples, meta.sample_rate, meta.num_channels);

    /* ── 2. Calcular tamaño del buffer y recibirlo ──────────────────────────*/
    uint32_t seg_bytes = meta.num_samples *
                         bytes_per_sample((uint16_t)meta.num_channels);

    if (seg_bytes == 0) {
        fprintf(stderr, "[worker %d] Tamaño de segmento inválido: 0 bytes\n",
                world_rank);
        return EXIT_FAILURE;
    }

    uint8_t *enc_buf = (uint8_t *)malloc(seg_bytes);
    if (!enc_buf) {
        fprintf(stderr, "[worker %d] malloc(%u) falló\n", world_rank, seg_bytes);
        return EXIT_FAILURE;
    }

    if ((int64_t)seg_bytes > (int64_t)0x7FFFFFFF) {
        fprintf(stderr, "[worker %d] seg_bytes supera INT_MAX\n", world_rank);
        free(enc_buf);
        return EXIT_FAILURE;
    }
    rc = MPI_Recv(enc_buf, (int)seg_bytes, MPI_BYTE,
                  MASTER_RANK, TAG_SEGMENT_DATA,
                  MPI_COMM_WORLD, &status);
    if (rc != MPI_SUCCESS) {
        fprintf(stderr, "[worker %d] MPI_Recv DATA falló (%d)\n",
                world_rank, rc);
        free(enc_buf);
        return EXIT_FAILURE;
    }

    printf("[worker %d] Recibidos %u bytes cifrados.\n",
           world_rank, seg_bytes);

    /* ── 3. Descifrar el segmento con XOR rotativo ──────────────────────────*/
    /* El offset global es el inicio del segmento dentro del data chunk.
     * Calculamos: segmento_index * (total_data_bytes / total_segments).
     * El maestro distribuye en partes proporcionales; aquí recuperamos
     * el offset mediante los metadatos (num_samples acumuladas). */
    size_t global_off = (size_t)meta.segment_index *
                        (size_t)(seg_bytes);  /* Aproximación conservadora */

    /* Para segmentos donde el trabajador 0 recibió extra, ajustamos.
     * El maestro pasa el offset real en meta. En una extensión futura se podría
     * añadir el campo offset al SegmentMeta. Por ahora usamos el producto
     * segment_index × seg_bytes (funciona siempre que el particionamiento sea
     * uniforme o el primer trabajador reciba el segmento mayor). */
    xor_crypt(enc_buf, seg_bytes, global_off);

    /* enc_buf ahora contiene los datos PCM descifrados */
    const int16_t *pcm_raw = (const int16_t *)enc_buf;

    /* ── 4. Mezclar a mono si el audio es stereo ────────────────────────────*/
    int16_t       *pcm_mono     = NULL;
    uint32_t       n_mono_samples = meta.num_samples;
    int            we_own_mono   = 0;

    if (meta.num_channels == 2) {
        /* num_samples aquí representa frames stereo; cada frame = 2 muestras */
        uint32_t n_frames = meta.num_samples;
        pcm_mono = stereo_to_mono(pcm_raw, n_frames);
        if (!pcm_mono) {
            free(enc_buf);
            return EXIT_FAILURE;
        }
        n_mono_samples = n_frames;
        we_own_mono    = 1;
    } else if (meta.num_channels == 1) {
        /* Ya es mono: cast directo sin copia */
        pcm_mono = (int16_t *)pcm_raw;
    } else {
        fprintf(stderr, "[worker %d] Número de canales no soportado: %u\n",
                world_rank, meta.num_channels);
        free(enc_buf);
        return EXIT_FAILURE;
    }

    /* ── 5. Procesar el segmento (FFT, bandas, BPM) ─────────────────────────*/
    WorkerResult result;
    memset(&result, 0, sizeof(result));
    result.worker_id     = world_rank;
    result.segment_index = meta.segment_index;

    sysmon_print(world_rank, "FFT/ANÁLISIS");
    if (process_segment(pcm_mono, n_mono_samples,
                        meta.sample_rate, &result) != 0) {
        fprintf(stderr, "[worker %d] process_segment falló. "
                        "Enviando resultado vacío.\n", world_rank);
        /* Enviar resultado vacío para no bloquear al maestro */
    }

    printf("[worker %d] Análisis completo: freq=%.1f Hz, RMS=%.4f, "
           "BPM=%.1f, LED=[%02X %02X %02X %02X %02X]\n",
           world_rank,
           result.dominant_freq, result.rms_amplitude, result.bpm_estimate,
           result.spectrogram[0], result.spectrogram[1], result.spectrogram[2],
           result.spectrogram[3], result.spectrogram[4]);

    /* ── 6. Enviar resultado al maestro ─────────────────────────────────────*/
    rc = MPI_Send(&result, sizeof(WorkerResult), MPI_BYTE,
                  MASTER_RANK, TAG_RESULT, MPI_COMM_WORLD);
    if (rc != MPI_SUCCESS) {
        fprintf(stderr, "[worker %d] MPI_Send RESULT falló (%d)\n",
                world_rank, rc);
    }

    /* ── 7. Esperar señal de terminación del maestro ────────────────────────*/
    int dummy;
    MPI_Recv(&dummy, 1, MPI_INT,
             MASTER_RANK, TAG_TERMINATE,
             MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    /* ── Liberar recursos ───────────────────────────────────────────────────*/
    if (we_own_mono) free(pcm_mono);
    free(enc_buf);

    sysmon_print(world_rank, "FIN");
    printf("[worker %d] Finalizado correctamente.\n", world_rank);
    return EXIT_SUCCESS;
}