/*******************************************************************************
 * libaudio_stub.c — Stub de libaudio.a para compilar sin el hardware real
 *
 * Este archivo SOLO se usa durante el desarrollo o en nodos sin /dev/audiousb.
 * En producción se enlaza con la libaudio.a real que maneja el driver de kernel.
 *
 * Para compilar con el stub:
 *   gcc -c libaudio_stub.c -o libaudio_stub.o
 *   ar rcs libaudio.a libaudio_stub.o
 *
 * Para compilar con la librería real:
 *   Enlazar directamente con -laudio -L<ruta>
 ******************************************************************************/

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "../include/common.h"

static int audiousb_fd = -1;

int audiousb_open(void)
{
    audiousb_fd = open("/dev/audiousb", O_WRONLY);
    if (audiousb_fd < 0) {
        fprintf(stderr, "[libaudio] open(/dev/audiousb): %s\n", strerror(errno));
        return -1;
    }
    printf("[libaudio] /dev/audiousb abierto (fd=%d)\n", audiousb_fd);
    return 0;
}

int audiousb_send_frame(const uint8_t frame[LED_COLS])
{
    if (audiousb_fd < 0) {
        fprintf(stderr, "[libaudio] send_frame: dispositivo no abierto\n");
        return -1;
    }

    ssize_t written = write(audiousb_fd, frame, LED_COLS);
    if (written != LED_COLS) {
        fprintf(stderr, "[libaudio] write(%d bytes): solo %zd escritos: %s\n",
                LED_COLS, written, strerror(errno));
        return -1;
    }
    return 0;
}

void audiousb_close(void)
{
    if (audiousb_fd >= 0) {
        close(audiousb_fd);
        audiousb_fd = -1;
        printf("[libaudio] /dev/audiousb cerrado\n");
    }
}