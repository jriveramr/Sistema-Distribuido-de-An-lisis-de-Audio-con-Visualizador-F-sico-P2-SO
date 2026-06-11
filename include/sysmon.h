/*******************************************************************************
 * sysmon.h — Monitor de recursos del sistema por nodo
 *
 * Requisito: "En cada nodo se debe mostrar el monitor del sistema
 *             para ver el consumo de recursos."
 *
 * Usa getrusage() (POSIX) y /proc/self/status (Linux) para obtener:
 *   - CPU user + system time
 *   - RAM residente (RSS)
 *   - Fallos de página
 *   - Máximo RSS histórico
 ******************************************************************************/

#ifndef SYSMON_H
#define SYSMON_H

#include <sys/resource.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct {
    double   cpu_user_sec;    /* Tiempo CPU en modo usuario (s)     */
    double   cpu_sys_sec;     /* Tiempo CPU en modo kernel (s)      */
    long     rss_kb;          /* RAM residente actual (KB)          */
    long     rss_peak_kb;     /* RAM residente máxima histórica(KB) */
    long     page_faults;     /* Fallos de página (sin I/O)         */
    long     vol_ctx_sw;      /* Cambios de contexto voluntarios    */
    long     invol_ctx_sw;    /* Cambios de contexto involuntarios  */
} SysStats;

/*
 * sysmon_read — Lee las estadísticas actuales del proceso.
 * Combina getrusage() con /proc/self/status para RSS en tiempo real.
 */
static inline int sysmon_read(SysStats *s)
{
    if (!s) return -1;
    memset(s, 0, sizeof(*s));

    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) return -1;

    s->cpu_user_sec  = (double)ru.ru_utime.tv_sec +
                       (double)ru.ru_utime.tv_usec * 1e-6;
    s->cpu_sys_sec   = (double)ru.ru_stime.tv_sec +
                       (double)ru.ru_stime.tv_usec * 1e-6;
    s->rss_peak_kb   = ru.ru_maxrss;          /* Linux: en KB */
    s->page_faults   = ru.ru_minflt;
    s->vol_ctx_sw    = ru.ru_nvcsw;
    s->invol_ctx_sw  = ru.ru_nivcsw;

    /* RSS actual desde /proc/self/status (más preciso que rusage en Linux) */
    FILE *fp = fopen("/proc/self/status", "r");
    if (fp) {
        char line[128];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "VmRSS:", 6) == 0) {
                s->rss_kb = atol(line + 6);
                break;
            }
        }
        fclose(fp);
    } else {
        s->rss_kb = s->rss_peak_kb;   /* Fallback */
    }

    return 0;
}

/*
 * sysmon_print — Imprime el monitor de recursos en stdout.
 *
 * @param rank   Rango MPI del proceso (0 = maestro)
 * @param label  Etiqueta de la fase ("INICIO", "ANÁLISIS FFT", "FIN", etc.)
 */
static inline void sysmon_print(int rank, const char *label)
{
    SysStats s;
    const char *role = (rank == 0) ? "master" : "worker";

    if (sysmon_read(&s) != 0) {
        fprintf(stderr, "[sysmon %s %d] Error leyendo estadísticas\n",
                role, rank);
        return;
    }

    printf("[sysmon %s %d | %-18s] "
           "CPU: user=%.3fs sys=%.3fs | "
           "RAM: rss=%ld KB peak=%ld KB | "
           "PF: %ld | CTX: vol=%ld invol=%ld\n",
           role, rank, label,
           s.cpu_user_sec, s.cpu_sys_sec,
           s.rss_kb, s.rss_peak_kb,
           s.page_faults,
           s.vol_ctx_sw, s.invol_ctx_sw);

    fflush(stdout);
}

#endif /* SYSMON_H */