#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>

int main()
{
    int fd;
    uint8_t frame[5];
    char respuesta[256];
    int ret;
    int i;

    fd = open("/dev/audiousb3", O_RDWR);
    if (fd < 0) {
        perror("No se pudo abrir /dev/audiousb3");
        return 1;
    }

    printf("Dispositivo abierto\n\n");

    frame[0] = 0;
    frame[1] = 2;
    frame[2] = 3;
    frame[3] = 4;
    frame[4] = 5;

    ret = write(fd, frame, 5);
    if (ret < 0) {
        perror("Error escribiendo");
        close(fd);
        return 1;
    }
    printf("Enviados %d bytes\n\n", ret);

    /* Leer 20 veces con pausa entre cada lectura */
    for (i = 0; i < 20; i++) {
        usleep(200000);
        memset(respuesta, 0, sizeof(respuesta));
        ret = read(fd, respuesta, sizeof(respuesta) - 1);
        if (ret > (int)sizeof(int)) {
            printf("%s", respuesta);
        }
    }
    printf("\n");

    close(fd);
    return 0;
}