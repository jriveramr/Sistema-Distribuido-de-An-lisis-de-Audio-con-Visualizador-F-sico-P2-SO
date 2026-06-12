/*******************************************************************************
 * main.c — Punto de entrada unificado del Sistema de Análisis de Audio
 *
 * Un solo binario (audio_dist) para todos los procesos MPI.
 * El proceso con rango 0 ejecuta la lógica de maestro.
 * Los procesos con rango >= 1 ejecutan la lógica de trabajador.
 *
 * Todos los nodos lanzan el mismo
 * ejecutable y cada uno descubre su rol mediante MPI_Comm_rank.
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>

/* Declaradas en master_logic.c y worker_logic.c */
int master_main(int argc, char *argv[], int world_rank, int world_size);
int worker_main(int world_rank, int world_size);

int main(int argc, char *argv[])
{
    MPI_Init(&argc, &argv);

    int world_rank, world_size;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    if (world_size < 2) {
        if (world_rank == 0)
            fprintf(stderr, "[main] Se necesitan al menos 2 procesos MPI.\n"
                            "       Uso: mpirun -np <N> ./audio_dist archivo.wav\n");
        MPI_Finalize();
        return EXIT_FAILURE;
    }

    int ret;
    if (world_rank == 0)
        ret = master_main(argc, argv, world_rank, world_size);
    else
        ret = worker_main(world_rank, world_size);

    MPI_Finalize();
    return ret;
}