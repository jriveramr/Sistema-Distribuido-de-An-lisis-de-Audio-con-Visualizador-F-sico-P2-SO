/*******************************************************************************
 * audiousb.h — Biblioteca de usuario para comunicación con /dev/audiousb
 ******************************************************************************/

#ifndef AUDIOUSB_H
#define AUDIOUSB_H

#include <stdint.h>
#include <stdlib.h>
#include "common.h"

/* Funciones principales */
int         audiousb_open(void);
int         audiousb_send_frame(const uint8_t frame[LED_COLS]);
ssize_t     audiousb_read_response(void *buffer, size_t size);
int         audiousb_wait_ack(void);
int         audiousb_flush(void);
int         audiousb_reset(void);
int         audiousb_get_status(void);
void        audiousb_close(void);

#endif /* AUDIOUSB_H */