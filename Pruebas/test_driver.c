/*
 * test_driver.c - Programa de prueba para el driver audiousb
 * Envía frames de 7 bytes y lee la respuesta del Arduino
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#define DEVICE "/dev/audiousb3"
#define FRAME_SIZE 7

void enviar_frame(int fd, uint8_t frame[], const char *nombre)
{
    int ret;
    char respuesta[256];

    printf("%s\n", nombre);
    printf("Enviando: [%d, %d, %d, %d, %d, %d, %d]\n",
           frame[0], frame[1], frame[2], frame[3],
           frame[4], frame[5], frame[6]);

    ret = write(fd, frame, FRAME_SIZE);
    if (ret < 0) {
        perror("Error escribiendo");
        return;
    }
    printf("Enviados %d bytes\n", ret);

    /* Esperar respuesta del Arduino */
    usleep(500000);

    memset(respuesta, 0, sizeof(respuesta));
    ret = read(fd, respuesta, sizeof(respuesta) - 1);
    if (ret > 0)
        printf("Arduino responde: %s\n", respuesta);
    else
        printf("Sin respuesta del Arduino\n");

    printf("\n");
}

int main()
{
    int fd;
    uint8_t frame[FRAME_SIZE];

    fd = open(DEVICE, O_RDWR);
    if (fd < 0) {
        perror("No se pudo abrir el dispositivo");
        return 1;
    }

    printf("Dispositivo abierto\n\n");

    /* Prueba 1: escalonado */
    frame[6] = 7;
    frame[5] = 6;
    frame[4] = 5;
    frame[3] = 4;
    frame[2] = 3;
    frame[1] = 2;
    frame[0] = 1;
    enviar_frame(fd, frame, "Prueba 1: escalonado");
    sleep(2);

    frame[0] = 0;
    frame[1] = 1;
    frame[2] = 2;
    frame[3] = 3;
    frame[4] = 4;
    frame[5] = 5;
    frame[6] = 6;
    enviar_frame(fd, frame, "Prueba 2: cero a seis");
    sleep(2);

    close(fd);
    printf("Dispositivo cerrado\n");

    return 0;
}