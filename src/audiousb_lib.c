/*******************************************************************************
 * audiousb_lib.c — Implementación de la biblioteca de acceso al driver
 *
 * Única capa del sistema que llama a open/write/read/close sobre
 * /dev/audiousb. El resto del código usa las funciones de esta biblioteca.
 ******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "../include/audiousb_lib.h"

int audiousb_open(const char *dev)
{
    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "[audiousb] No se pudo abrir %s: %s\n",
                dev, strerror(errno));
        fprintf(stderr, "[audiousb] Verifica que el módulo esté cargado: "
                        "sudo insmod audiousb.ko\n");
    }
    return fd;
}

void audiousb_close(int fd)
{
    if (close(fd) != 0)
        fprintf(stderr, "[audiousb] Error cerrando dispositivo: %s\n",
                strerror(errno));
}

ssize_t audiousb_write(int fd, const uint8_t *frame, int len)
{
    ssize_t written = write(fd, frame, (size_t)len);
    if (written != (ssize_t)len)
        fprintf(stderr, "[audiousb] write(): esperados %d bytes, "
                        "escritos %zd: %s\n", len, written, strerror(errno));
    return written;
}

ssize_t audiousb_read(int fd, unsigned char *buf, size_t bufsz)
{
    ssize_t n = read(fd, buf, bufsz);
    if (n < 0)
        fprintf(stderr, "[audiousb] read(): %s\n", strerror(errno));
    else if (n == 0)
        fprintf(stderr, "[audiousb] read(): el dispositivo no envió datos\n");
    return n;
}

int audiousb_send_frame(int fd, const uint8_t *frame, int cols)
{
    ssize_t written = audiousb_write(fd, frame, cols);
    if (written != (ssize_t)cols)
        return -1;

    printf("[audiousb] Frame [%d %d %d %d %d %d %d] enviado correctamente.\n",
           frame[0], frame[1], frame[2], frame[3],
           frame[4], frame[5], frame[6]);

    unsigned char buf[AUDIOUSB_ACK_BUFSZ];
    ssize_t n = audiousb_read(fd, buf, sizeof(buf));
    if (n < 0)
        return -1;
    if (n == 0) {
        fprintf(stderr, "[audiousb] Arduino no envió respuesta.\n");
        return -1;
    }

    printf("[audiousb] ACK del Arduino: %zd byte(s), primer byte = 0x%02X\n",
           n, buf[0]);

    if (buf[0] == AUDIOUSB_ACK_OK) {
        printf("[audiousb] Arduino confirmó: LEDs actualizados correctamente.\n");
        if (n > 1) {
            buf[n] = '\0';
            printf("[audiousb] Mensaje Arduino: %s", (char *)(buf + 1));
        }
        return 0;
    }

    fprintf(stderr, "[audiousb] Arduino reportó error: código 0x%02X\n", buf[0]);
    return 1;
}
