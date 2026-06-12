/*******************************************************************************
 * audiousb_lib.h — API de la biblioteca de acceso al driver /dev/audiousb
 *
 * Esta biblioteca es la única capa que interactúa con el driver del kernel.
 * El resto del sistema (master.c) usa estas funciones en lugar de llamar
 * directamente a open/write/read/close sobre el dispositivo.
 ******************************************************************************/

#ifndef AUDIOUSB_LIB_H
#define AUDIOUSB_LIB_H

#include <stdint.h>
#include <sys/types.h>

#define AUDIOUSB_DEV      "/dev/audiousb"
#define AUDIOUSB_ACK_OK   0x01
#define AUDIOUSB_ACK_BUFSZ 64

/*
 * audiousb_open — Abre el dispositivo en modo lectura+escritura.
 * Retorna el file descriptor, o -1 en error (errno queda seteado).
 */
int audiousb_open(const char *dev);

/*
 * audiousb_close — Cierra el file descriptor del dispositivo.
 */
void audiousb_close(int fd);

/*
 * audiousb_write — Envía `len` bytes del frame al Arduino vía USB bulk OUT.
 * Retorna el número de bytes escritos, o -1 en error.
 */
ssize_t audiousb_write(int fd, const uint8_t *frame, int len);

/*
 * audiousb_read — Lee la respuesta del Arduino vía USB bulk IN.
 * Almacena hasta `bufsz` bytes en `buf`.
 * Retorna el número de bytes leídos, o -1 en error.
 */
ssize_t audiousb_read(int fd, unsigned char *buf, size_t bufsz);

/*
 * audiousb_send_frame — Operación completa: write + read + log.
 * Envía el frame, espera el ACK del Arduino e imprime el resultado.
 * Retorna  0 si el Arduino confirmó éxito (ACK == 0x01).
 * Retorna -1 en error de escritura/lectura.
 * Retorna  1 si el Arduino respondió con código de error.
 */
int audiousb_send_frame(int fd, const uint8_t *frame, int cols);

#endif /* AUDIOUSB_LIB_H */
