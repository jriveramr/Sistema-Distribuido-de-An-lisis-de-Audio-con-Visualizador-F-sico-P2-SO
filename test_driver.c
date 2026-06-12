/*
 * test_driver.c - Programa de prueba para el driver audiousb
 * Envía frames de 7 bytes al Arduino para probar la matriz 7x7
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

#define DEVICE "/dev/audiousb3"
#define FRAME_SIZE 7

int main()
{
    int fd;
    uint8_t frame[FRAME_SIZE];
    int ret;

    fd = open(DEVICE, O_RDWR);
    if (fd < 0) {
        perror("No se pudo abrir el dispositivo");
        return 1;
    }

    printf("Dispositivo abierto\n\n");

    /* Prueba 1: escalonado */
    printf("Prueba 1: escalonado\n");
    frame[3] = 7;
    frame[4] = 6;
    frame[5] = 5;
    frame[6] = 4;
    frame[0] = 3;
    frame[1] = 2;
    frame[2] = 1;
    printf("Enviando: [%d, %d, %d, %d, %d, %d, %d]\n",
           frame[0], frame[1], frame[2], frame[3],
           frame[4], frame[5], frame[6]);
    ret = write(fd, frame, FRAME_SIZE);
    if (ret < 0)
        perror("Error escribiendo");
    else
        printf("Enviados %d bytes\n", ret);

    sleep(3);

    /* Prueba 2: todo al maximo 
    printf("\nPrueba 2: todo al maximo\n");
    memset(frame, 7, FRAME_SIZE);
    printf("Enviando: [%d, %d, %d, %d, %d, %d, %d]\n",
           frame[0], frame[1], frame[2], frame[3],
           frame[4], frame[5], frame[6]);
    ret = write(fd, frame, FRAME_SIZE);
    if (ret < 0)
        perror("Error escribiendo");
    else
        printf("Enviados %d bytes\n", ret);

    sleep(3);

     Prueba 3: patron alternado 
    printf("\nPrueba 3: patron alternado\n");
    frame[0] = 7;
    frame[1] = 2;
    frame[2] = 7;
    frame[3] = 2;
    frame[4] = 7;
    frame[5] = 2;
    frame[6] = 7;
    printf("Enviando: [%d, %d, %d, %d, %d, %d, %d]\n",
           frame[0], frame[1], frame[2], frame[3],
           frame[4], frame[5], frame[6]);
    ret = write(fd, frame, FRAME_SIZE);
    if (ret < 0)
        perror("Error escribiendo");
    else
        printf("Enviados %d bytes\n", ret);

    sleep(3);

    Prueba 4: todo apagado 
    printf("\nPrueba 4: todo apagado\n");
    memset(frame, 0, FRAME_SIZE);
    printf("Enviando: [%d, %d, %d, %d, %d, %d, %d]\n",
           frame[0], frame[1], frame[2], frame[3],
           frame[4], frame[5], frame[6]);
    ret = write(fd, frame, FRAME_SIZE);
    if (ret < 0)
        perror("Error escribiendo");
    else
        printf("Enviados %d bytes\n", ret);
        */
    close(fd);
    printf("\nDispositivo cerrado\n");

    return 0;
}