/*******************************************************************************
 * master.c — Nodo Maestro del Sistema de Análisis de Audio Distribuido
 *
 * Responsabilidades:
 *   1. Leer y validar la cabecera WAV (PCM 16-bit).
 *   2. Particionar manualmente el data chunk entre N trabajadores.
 *   3. Cifrar cada segmento con XOR rotativo y enviarlo vía MPI_Send.
 *   4. Guardar en disco el archivo cifrado y el descifrado.
 *   5. Recibir resultados parciales de cada trabajador con MPI_Recv.
 *   6. Consolidar, clasificar y generar el frame LED de 5 columnas.
 *   7. Enviar el frame a la matriz LED mediante libaudio.a (/dev/audiousb).
 *   8. Notificar a los trabajadores que terminen.
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <errno.h>

#include <mpi.h>

#define AUDIO_DEFINE_CLASS_NAMES
#include "../include/common.h"
#include "../include/crypto.h"
#include "../include/fft.h"

/* ─── Prototipo de libaudio.a (interacción con /dev/audiousb) ──────────────*/
/* Declaramos extern para enlazar con libaudio.a sin su header propio.        */
extern int  audiousb_open(void);
extern int  audiousb_send_frame(const uint8_t frame[LED_COLS]);
extern void audiousb_close(void);

/* ══════════════════════════════════════════════════════════════════════════════
 * Lectura del header WAV y búsqueda del chunk "data"
 * ══════════════════════════════════════════════════════════════════════════════*/

typedef struct {
    WavHeader hdr;
    long      data_offset;   /* Posición en el archivo donde empiezan los datos */
    uint32_t  data_bytes;    /* Tamaño del chunk data en bytes                  */
} WavInfo;

static int wav_read_info(FILE *fp, WavInfo *info)
{
    if (!fp || !info) return -1;

    /* Leer cabecera principal */
    if (fread(&info->hdr, sizeof(WavHeader), 1, fp) != 1) {
        fprintf(stderr, "[master] Error leyendo cabecera WAV: %s\n", strerror(errno));
        return -1;
    }

    /* Validar firmas */
    if (memcmp(info->hdr.riff_id, "RIFF", 4) != 0) {
        fprintf(stderr, "[master] No es un archivo RIFF válido\n");
        return -1;
    }
    if (memcmp(info->hdr.wave_id, "WAVE", 4) != 0) {
        fprintf(stderr, "[master] No es un archivo WAVE válido\n");
        return -1;
    }
    if (info->hdr.audio_format != 1) {
        fprintf(stderr, "[master] Solo se soporta PCM lineal (formato 1), "
                        "encontrado: %u\n", info->hdr.audio_format);
        return -1;
    }
    if (info->hdr.bits_per_sample != 16) {
        fprintf(stderr, "[master] Solo se soporta 16 bits por muestra, "
                        "encontrado: %u\n", info->hdr.bits_per_sample);
        return -1;
    }

    /* Saltar chunks intermedios hasta encontrar "data" */
    char   chunk_id[4];
    uint32_t chunk_size;

    /* Si el fmt_size > 16, puede haber bytes extra en el chunk fmt */
    if (info->hdr.fmt_size > 16) {
        if (fseek(fp, (long)(info->hdr.fmt_size - 16), SEEK_CUR) != 0) {
            fprintf(stderr, "[master] fseek en fmt extra: %s\n", strerror(errno));
            return -1;
        }
    }

    /* Buscar chunk "data" */
    while (1) {
        if (fread(chunk_id,   1, 4, fp) != 4) break;
        if (fread(&chunk_size, 4, 1, fp) != 1) break;

        if (memcmp(chunk_id, "data", 4) == 0) {
            info->data_offset = ftell(fp);
            info->data_bytes  = chunk_size;
            if (info->data_offset < 0) {
                fprintf(stderr, "[master] ftell falló: %s\n", strerror(errno));
                return -1;
            }
            return 0;  /* Éxito */
        }

        /* Saltar chunk desconocido (respetar alineación a 2 bytes) */
        long skip = (long)chunk_size + (chunk_size & 1u);
        if (fseek(fp, skip, SEEK_CUR) != 0) {
            fprintf(stderr, "[master] fseek saltando chunk '%c%c%c%c': %s\n",
                    chunk_id[0], chunk_id[1], chunk_id[2], chunk_id[3],
                    strerror(errno));
            return -1;
        }
    }

    fprintf(stderr, "[master] No se encontró el chunk 'data'\n");
    return -1;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Particionamiento del data chunk entre N trabajadores
 *
 * Garantiza que cada segmento empiece en un límite de bloque PCM completo
 * (múltiplo de block_align) para no cortar muestras en mitad.
 * ══════════════════════════════════════════════════════════════════════════════*/

typedef struct {
    uint32_t offset_bytes;   /* Offset dentro del data chunk (bytes) */
    uint32_t length_bytes;   /* Longitud del segmento (bytes)        */
} Partition;

static int build_partitions(uint32_t total_bytes, uint16_t block_align,
                             int n_workers, Partition **out, int *out_count)
{
    if (!out || !out_count || n_workers <= 0 || block_align == 0) return -1;

    /* Asegurar que el total sea múltiplo de block_align */
    uint32_t aligned_total = (total_bytes / block_align) * block_align;

    /* Bytes base por trabajador */
    uint32_t base    = aligned_total / (uint32_t)n_workers;
    uint32_t leftover = aligned_total % (uint32_t)n_workers;

    /* Ajustar base al múltiplo de block_align inferior */
    base = (base / block_align) * block_align;

    Partition *parts = (Partition *)calloc((size_t)n_workers, sizeof(Partition));
    if (!parts) {
        fprintf(stderr, "[master] calloc particiones falló\n");
        return -1;
    }

    uint32_t offset = 0;
    for (int w = 0; w < n_workers; ++w) {
        parts[w].offset_bytes = offset;
        /* Distribuir el sobrante al primer trabajador */
        uint32_t extra = (w == 0) ? leftover : 0;
        /* Ajustar extra a múltiplo de block_align */
        extra = (extra / block_align) * block_align;
        parts[w].length_bytes = base + extra;
        offset += parts[w].length_bytes;
    }


    *out       = parts;
    *out_count = n_workers;
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Guardar en disco: archivo cifrado y archivo descifrado
 * ══════════════════════════════════════════════════════════════════════════════*/

static int save_file(const char *path, const uint8_t *data, size_t len)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) {
        fprintf(stderr, "[master] No se pudo abrir '%s' para escritura: %s\n",
                path, strerror(errno));
        return -1;
    }
    if (fwrite(data, 1, len, fp) != len) {
        fprintf(stderr, "[master] Error escribiendo '%s': %s\n",
                path, strerror(errno));
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * Consolidación de resultados y clasificación de audio
 * ══════════════════════════════════════════════════════════════════════════════*/

static void consolidate_results(WorkerResult *results, int n_results,
                                 GlobalResult *global)
{
    memset(global, 0, sizeof(GlobalResult));
    if (n_results <= 0) return;

    double sum_dom_freq   = 0.0;
    double sum_rms        = 0.0;
    double sum_sb         = 0.0;
    double sum_mid        = 0.0;
    double sum_um         = 0.0;
    double sum_high       = 0.0;
    double sum_bpm        = 0.0;
    int    bpm_count      = 0;

    for (int i = 0; i < n_results; ++i) {
        sum_dom_freq += results[i].dominant_freq;
        sum_rms      += results[i].rms_amplitude;
        sum_sb       += results[i].energy_subbass;
        sum_mid      += results[i].energy_mid;
        sum_um       += results[i].energy_uppermid;
        sum_high     += results[i].energy_high;
        if (results[i].bpm_estimate > 0.0) {
            sum_bpm  += results[i].bpm_estimate;
            ++bpm_count;
        }
    }

    double n = (double)n_results;
    global->dominant_freq  = sum_dom_freq / n;
    global->rms_amplitude  = sum_rms      / n;
    global->energy_subbass = sum_sb       / n;
    global->energy_mid     = sum_mid      / n;
    global->energy_uppermid= sum_um       / n;
    global->energy_high    = sum_high     / n;
    global->bpm_estimate   = (bpm_count > 0) ? (sum_bpm / (double)bpm_count) : 0.0;

    /* ── Clasificación heurística ───────────────────────────────────────────── */
    /* SILENCIO: amplitud RMS muy baja */
    if (global->rms_amplitude < 0.01) {
        global->classification = CLASS_SILENCE;
    }
    /* RUIDO: distribución de energía muy uniforme entre bandas */
    else {
        double max_band = global->energy_subbass;
        if (global->energy_mid      > max_band) max_band = global->energy_mid;
        if (global->energy_uppermid > max_band) max_band = global->energy_uppermid;
        if (global->energy_high     > max_band) max_band = global->energy_high;

        double spread = max_band - global->energy_subbass;
        if (spread < 0.05 && global->energy_high > 0.3) {
            global->classification = CLASS_NOISE;
        }
        /* VOZ: dominancia de frecuencias medias (300-3000 Hz) y BPM bajo */
        else if (global->energy_mid > 0.4 &&
                 global->dominant_freq > 80.0 &&
                 global->dominant_freq < 4000.0 &&
                 (global->bpm_estimate < 80.0 || global->bpm_estimate == 0.0)) {
            global->classification = CLASS_VOICE;
        }
        /* MÚSICA: BPM detectable y energía distribuida en varias bandas */
        else if (global->bpm_estimate > 60.0 &&
                 (global->energy_subbass + global->energy_mid) > 0.4) {
            global->classification = CLASS_MUSIC;
        }
        else {
            /* Fallback: elegir según banda dominante */
            if (global->energy_mid > 0.35)
                global->classification = CLASS_VOICE;
            else if (global->energy_subbass > 0.3)
                global->classification = CLASS_MUSIC;
            else
                global->classification = CLASS_NOISE;
        }
    }

    /* ── Frame LED global ───────────────────────────────────────────────────── */
    build_led_frame(global->energy_subbass,  global->energy_mid,
                    global->energy_uppermid, global->energy_high,
                    global->rms_amplitude,   global->led_frame);
}

/* ══════════════════════════════════════════════════════════════════════════════
 * master_main — Lógica del maestro, invocado desde main.c cuando rank == 0.
 * MPI ya está inicializado; NO llamar MPI_Init/Finalize aquí.
 * ══════════════════════════════════════════════════════════════════════════════*/

int master_main(int argc, char *argv[], int world_rank, int world_size)
{
    (void)world_rank;   /* Siempre es 0; parámetro para coherencia de firma */

    if (argc < 2) {
        fprintf(stderr, "Uso: mpirun -np <N> ./audio_dist <archivo.wav>\n");
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        return EXIT_FAILURE;
    }

    const char *wav_path = argv[1];
    int n_workers = world_size - 1;  /* Rango 1..N-1 son trabajadores */

    printf("[master] Iniciando con %d trabajador(es). Archivo: %s\n",
           n_workers, wav_path);

    /* ── 1. Abrir y analizar el archivo WAV ─────────────────────────────────*/
    FILE *wav_fp = fopen(wav_path, "rb");
    if (!wav_fp) {
        fprintf(stderr, "[master] No se pudo abrir '%s': %s\n",
                wav_path, strerror(errno));
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        return EXIT_FAILURE;
    }

    WavInfo winfo;
    if (wav_read_info(wav_fp, &winfo) != 0) {
        fclose(wav_fp);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        return EXIT_FAILURE;
    }

    printf("[master] WAV: %u Hz, %u canales, %u bits, data_bytes=%u\n",
           winfo.hdr.sample_rate, winfo.hdr.num_channels,
           winfo.hdr.bits_per_sample, winfo.data_bytes);

    /* ── 2. Leer el data chunk completo en memoria ──────────────────────────*/
    if (fseek(wav_fp, winfo.data_offset, SEEK_SET) != 0) {
        fprintf(stderr, "[master] fseek al data chunk: %s\n", strerror(errno));
        fclose(wav_fp);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        return EXIT_FAILURE;
    }

    uint8_t *raw_data = (uint8_t *)malloc(winfo.data_bytes);
    if (!raw_data) {
        fprintf(stderr, "[master] malloc(%u) para data chunk falló\n",
                winfo.data_bytes);
        fclose(wav_fp);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        return EXIT_FAILURE;
    }

    size_t bytes_read = fread(raw_data, 1, winfo.data_bytes, wav_fp);
    fclose(wav_fp);

    if (bytes_read != winfo.data_bytes) {
        fprintf(stderr, "[master] Solo se leyeron %zu de %u bytes del data chunk\n",
                bytes_read, winfo.data_bytes);
        free(raw_data);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        return EXIT_FAILURE;
    }

    /* ── 3. Guardar archivo descifrado (original) ───────────────────────────*/
    if (save_file("audio_plain.raw", raw_data, winfo.data_bytes) != 0) {
        fprintf(stderr, "[master] Advertencia: no se pudo guardar audio_plain.raw\n");
    } else {
        printf("[master] Datos originales guardados en audio_plain.raw\n");
    }

    /* ── 4. Cifrar el data chunk completo con XOR rotativo ──────────────────*/
    uint8_t *enc_data = xor_crypt_alloc(raw_data, winfo.data_bytes, 0);
    if (!enc_data) {
        free(raw_data);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        return EXIT_FAILURE;
    }

    if (save_file("audio_encrypted.raw", enc_data, winfo.data_bytes) != 0) {
        fprintf(stderr, "[master] Advertencia: no se pudo guardar audio_encrypted.raw\n");
    } else {
        printf("[master] Datos cifrados guardados en audio_encrypted.raw\n");
    }

    /* ── 5. Particionar manualmente entre trabajadores ──────────────────────*/
    Partition *parts    = NULL;
    int        n_parts  = 0;

    if (build_partitions(winfo.data_bytes, winfo.hdr.block_align,
                         n_workers, &parts, &n_parts) != 0) {
        free(raw_data);
        free(enc_data);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        return EXIT_FAILURE;
    }

    printf("[master] Particionamiento (%d segmentos):\n", n_parts);
    for (int i = 0; i < n_parts; ++i) {
        printf("  Segmento %d → offset=%u, length=%u bytes\n",
               i, parts[i].offset_bytes, parts[i].length_bytes);
    }

    /* ── 6. Enviar segmentos cifrados a cada trabajador (punto a punto) ─────*/
    for (int w = 0; w < n_workers; ++w) {
        int dest = w + 1;  /* Rango MPI del trabajador */

        SegmentMeta meta = {
            .worker_id      = dest,
            .sample_rate    = winfo.hdr.sample_rate,
            .num_channels   = winfo.hdr.num_channels,
            .num_samples    = parts[w].length_bytes /
                              bytes_per_sample(winfo.hdr.num_channels),
            .segment_index  = (uint32_t)w,
            .total_segments = (uint32_t)n_workers
        };

        /* Enviar metadatos */
        int rc = MPI_Send(&meta, sizeof(SegmentMeta), MPI_BYTE,
                          dest, TAG_SEGMENT_META, MPI_COMM_WORLD);
        if (rc != MPI_SUCCESS) {
            fprintf(stderr, "[master] MPI_Send META a rango %d falló (%d)\n",
                    dest, rc);
            /* Continuar con los demás */
            continue;
        }

        /* Enviar datos cifrados del segmento correspondiente */
        uint8_t *seg_ptr = enc_data + parts[w].offset_bytes;
        rc = MPI_Send(seg_ptr, (int)parts[w].length_bytes, MPI_BYTE,
                      dest, TAG_SEGMENT_DATA, MPI_COMM_WORLD);
        if (rc != MPI_SUCCESS) {
            fprintf(stderr, "[master] MPI_Send DATA a rango %d falló (%d)\n",
                    dest, rc);
        } else {
            printf("[master] → Enviado segmento %d al trabajador %d "
                   "(%u bytes cifrados)\n",
                   w, dest, parts[w].length_bytes);
        }
    }

    /* ── 7. Recibir resultados parciales de los trabajadores ────────────────*/
    WorkerResult *results = (WorkerResult *)calloc((size_t)n_workers,
                                                    sizeof(WorkerResult));
    if (!results) {
        fprintf(stderr, "[master] calloc results falló\n");
        free(raw_data); free(enc_data); free(parts);
        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
        return EXIT_FAILURE;
    }

    int received = 0;
    for (int w = 0; w < n_workers; ++w) {
        MPI_Status status;
        int rc = MPI_Recv(&results[w], sizeof(WorkerResult), MPI_BYTE,
                          MPI_ANY_SOURCE, TAG_RESULT,
                          MPI_COMM_WORLD, &status);
        if (rc != MPI_SUCCESS) {
            fprintf(stderr, "[master] MPI_Recv resultado #%d falló (%d)\n",
                    w, rc);
            continue;
        }
        ++received;
        printf("[master] ← Resultado del trabajador %d: "
               "freq=%.1f Hz, RMS=%.4f, BPM=%.1f\n",
               results[w].worker_id,
               results[w].dominant_freq,
               results[w].rms_amplitude,
               results[w].bpm_estimate);
    }

    /* ── 8. Notificar fin a todos los trabajadores ──────────────────────────*/
    int dummy = 0;
    for (int w = 1; w <= n_workers; ++w) {
        MPI_Send(&dummy, 1, MPI_INT, w, TAG_TERMINATE, MPI_COMM_WORLD);
    }

    /* ── 9. Consolidar resultados y clasificar ──────────────────────────────*/
    GlobalResult global;
    consolidate_results(results, received, &global);

    printf("\n[master] ══════════ RESULTADO GLOBAL ══════════\n");
    printf("  Frecuencia dominante : %.2f Hz\n",  global.dominant_freq);
    printf("  Amplitud RMS         : %.4f\n",     global.rms_amplitude);
    printf("  Energía Sub-bass     : %.4f\n",     global.energy_subbass);
    printf("  Energía Mid          : %.4f\n",     global.energy_mid);
    printf("  Energía Upper-mid    : %.4f\n",     global.energy_uppermid);
    printf("  Energía High         : %.4f\n",     global.energy_high);
    printf("  BPM estimado         : %.1f\n",     global.bpm_estimate);
    printf("  Clasificación        : %s\n",
           AudioClassName[global.classification]);
    printf("  Frame LED            : [%02X %02X %02X %02X %02X]\n",
           global.led_frame[0], global.led_frame[1], global.led_frame[2],
           global.led_frame[3], global.led_frame[4]);
    printf("═══════════════════════════════════════════════\n\n");

    /* ── 10. Enviar frame a la matriz LED vía libaudio.a ────────────────────*/
    if (audiousb_open() == 0) {
        if (audiousb_send_frame(global.led_frame) != 0) {
            fprintf(stderr, "[master] Error enviando frame al hardware LED\n");
        } else {
            printf("[master] Frame enviado a la matriz LED correctamente.\n");
        }
        audiousb_close();
    } else {
        fprintf(stderr, "[master] Advertencia: no se pudo abrir /dev/audiousb. "
                        "¿Está el driver cargado?\n");
    }

    /* ── Liberar recursos ───────────────────────────────────────────────────*/
    free(raw_data);
    free(enc_data);
    free(parts);
    free(results);

    printf("[master] Finalizado correctamente.\n");
    return EXIT_SUCCESS;
}