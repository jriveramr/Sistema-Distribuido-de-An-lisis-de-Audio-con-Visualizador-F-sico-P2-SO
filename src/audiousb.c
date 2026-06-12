/*******************************************************************************
 * audiousb.c — Biblioteca de usuario para comunicación con /dev/audiousb
 *
 * Esta biblioteca proporciona la interfaz de alto nivel para interactuar
 * con el driver del kernel USB que controla el Arduino con matriz LED 7x7.
 *
 * Compilación:
 *   gcc -c audiousb.c -o audiousb.o
 *   ar rcs libaudio.a audiousb.o
 *
 * Uso en el maestro:
 *   #include "audiousb.h"
 *   audiousb_open();
 *   audiousb_send_frame(frame_7x7);
 *   audiousb_read_response(response, sizeof(response));
 *   audiousb_close();
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "../include/common.h"

/* ─── Definiciones del protocolo con el Arduino ──────────────────────────────*/
#define DEVICE_PATH         "/dev/audiousb"
#define AUDIOUSB_FRAME_SIZE 7          /* 7 bytes: uno por columna (7x7) */
#define MAX_LEVEL           7          /* Nivel máximo por columna (7 filas) */
#define RESPONSE_SIZE       64         /* Tamaño del buffer de respuesta */

/* Comandos ioctl */
#define AUDIOUSB_IOC_MAGIC  'A'
#define AUDIOUSB_IOC_RESET   _IO(AUDIOUSB_IOC_MAGIC, 1)
#define AUDIOUSB_IOC_GETSTAT _IOR(AUDIOUSB_IOC_MAGIC, 2, int)

static int audiousb_fd = -1;
static char response_buffer[RESPONSE_SIZE];

/* ════════════════════════════════════════════════════════════════════════════
 * audiousb_open — Abre el dispositivo /dev/audiousb
 *
 * Retorna: 0 en éxito, -1 en error
 * ════════════════════════════════════════════════════════════════════════════*/
int audiousb_open(void)
{
    if (audiousb_fd >= 0) {
        fprintf(stderr, "[audiousb] El dispositivo ya está abierto (fd=%d)\n",
                audiousb_fd);
        return 0;
    }

    audiousb_fd = open(DEVICE_PATH, O_RDWR);
    if (audiousb_fd < 0) {
        fprintf(stderr, "[audiousb] Error abriendo %s: %s\n",
                DEVICE_PATH, strerror(errno));
        return -1;
    }

    printf("[audiousb] Dispositivo %s abierto (fd=%d)\n", DEVICE_PATH, audiousb_fd);
    return 0;
}

/* ════════════════════════════════════════════════════════════════════════════
 * audiousb_send_frame — Envía un frame de 7 bytes a la matriz LED 7x7
 *
 * Cada byte representa una columna. Los bits 0-6 indican qué filas encender.
 * Bit 0 = fila inferior, Bit 6 = fila superior.
 *
 * @param frame  Array de 7 bytes (uno por columna)
 * Retorna: 0 en éxito, -1 en error
 * ════════════════════════════════════════════════════════════════════════════*/
int audiousb_send_frame(const uint8_t frame[LED_COLS])
{
    uint8_t validated_frame[LED_COLS];
    int i;

    if (audiousb_fd < 0) {
        fprintf(stderr, "[audiousb] send_frame: dispositivo no abierto. "
                        "Llame audiousb_open() primero.\n");
        return -1;
    }

    /* Validar y limitar los niveles */
    for (i = 0; i < LED_COLS; i++) {
        validated_frame[i] = frame[i];
        /* Cada columna puede tener hasta MAX_LEVEL bits encendidos */
        if (validated_frame[i] > MAX_LEVEL) {
            validated_frame[i] = MAX_LEVEL;
        }
    }

    printf("[audiousb] Enviando frame 7x7: [%02X %02X %02X %02X %02X %02X %02X]\n",
           validated_frame[0], validated_frame[1], validated_frame[2],
           validated_frame[3], validated_frame[4], validated_frame[5],
           validated_frame[6]);

    ssize_t written = write(audiousb_fd, validated_frame, LED_COLS);
    if (written != LED_COLS) {
        fprintf(stderr, "[audiousb] write(%d bytes): solo %zd escritos: %s\n",
                LED_COLS, written, strerror(errno));
        return -1;
    }

    return 0;
}

/* ════════════════════════════════════════════════════════════════════════════
 * audiousb_read_response — Lee la respuesta del Arduino por USB
 *
 * El Arduino puede enviar datos de vuelta (por ejemplo, confirmación,
 * estado de los LEDs, o datos de sensores).
 *
 * @param buffer  Buffer donde almacenar la respuesta
 * @param size    Tamaño máximo a leer (debe ser >= RESPONSE_SIZE)
 * Retorna: Número de bytes leídos, -1 en error
 * ════════════════════════════════════════════════════════════════════════════*/
ssize_t audiousb_read_response(void *buffer, size_t size)
{
    ssize_t bytes_read;

    if (audiousb_fd < 0) {
        fprintf(stderr, "[audiousb] read_response: dispositivo no abierto\n");
        return -1;
    }

    if (size > RESPONSE_SIZE) {
        size = RESPONSE_SIZE;
    }

    bytes_read = read(audiousb_fd, buffer, size);
    if (bytes_read < 0) {
        fprintf(stderr, "[audiousb] read_response: %s\n", strerror(errno));
        return -1;
    }

    if (bytes_read > 0) {
        printf("[audiousb] Leídos %zd bytes del Arduino\n", bytes_read);
        /* Mostrar en hexdump para depuración */
        for (ssize_t i = 0; i < bytes_read; i++) {
            printf("%02X ", ((unsigned char*)buffer)[i]);
            if ((i + 1) % 16 == 0) printf("\n");
        }
        if (bytes_read % 16 != 0) printf("\n");
    }

    return bytes_read;
}

/* ════════════════════════════════════════════════════════════════════════════
 * audiousb_wait_ack — Espera confirmación del Arduino
 *
 * Bloquea hasta recibir un byte de confirmación (0x01 = ACK, 0x00 = NACK)
 *
 * Retorna: 1 si ACK, 0 si NACK, -1 en error
 * ════════════════════════════════════════════════════════════════════════════*/
int audiousb_wait_ack(void)
{
    uint8_t ack;
    ssize_t n;

    n = audiousb_read_response(&ack, 1);
    if (n <= 0) {
        return -1;
    }

    if (ack == 0x01) {
        printf("[audiousb] ACK recibido del Arduino\n");
        return 1;
    } else if (ack == 0x00) {
        printf("[audiousb] NACK recibido del Arduino\n");
        return 0;
    } else {
        printf("[audiousb] Respuesta inesperada: 0x%02X\n", ack);
        return -1;
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 * audiousb_flush — Limpia el buffer de entrada del dispositivo
 *
 * Retorna: 0 en éxito, -1 en error
 * ════════════════════════════════════════════════════════════════════════════*/
int audiousb_flush(void)
{
    uint8_t dummy[RESPONSE_SIZE];
    ssize_t n;

    if (audiousb_fd < 0) {
        return -1;
    }

    /* Leer todos los datos pendientes */
    while (1) {
        n = read(audiousb_fd, dummy, sizeof(dummy));
        if (n <= 0) break;
        printf("[audiousb] Flush: descartados %zd bytes\n", n);
    }

    return 0;
}

/* ════════════════════════════════════════════════════════════════════════════
 * audiousb_reset — Reinicia la comunicación USB (ioctl)
 *
 * Retorna: 0 en éxito, -1 en error
 * ════════════════════════════════════════════════════════════════════════════*/
int audiousb_reset(void)
{
    if (audiousb_fd < 0) {
        fprintf(stderr, "[audiousb] reset: dispositivo no abierto\n");
        return -1;
    }

    if (ioctl(audiousb_fd, AUDIOUSB_IOC_RESET) < 0) {
        fprintf(stderr, "[audiousb] ioctl(RESET) falló: %s\n", strerror(errno));
        return -1;
    }

    printf("[audiousb] Comunicación USB reiniciada\n");
    return 0;
}

/* ════════════════════════════════════════════════════════════════════════════
 * audiousb_get_status — Obtiene el estado del dispositivo (ioctl)
 *
 * Retorna: Estado (>=0) en éxito, -1 en error
 * ════════════════════════════════════════════════════════════════════════════*/
int audiousb_get_status(void)
{
    int status;

    if (audiousb_fd < 0) {
        fprintf(stderr, "[audiousb] get_status: dispositivo no abierto\n");
        return -1;
    }

    if (ioctl(audiousb_fd, AUDIOUSB_IOC_GETSTAT, &status) < 0) {
        fprintf(stderr, "[audiousb] ioctl(GETSTAT) falló: %s\n", strerror(errno));
        return -1;
    }

    printf("[audiousb] Estado del dispositivo: %d\n", status);
    return status;
}

/* ════════════════════════════════════════════════════════════════════════════
 * audiousb_close — Cierra el dispositivo
 * ════════════════════════════════════════════════════════════════════════════*/
void audiousb_close(void)
{
    if (audiousb_fd >= 0) {
        close(audiousb_fd);
        audiousb_fd = -1;
        printf("[audiousb] Dispositivo cerrado\n");
    }
}