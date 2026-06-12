# Makefile para Sistema de Análisis de Audio Distribuido
# Estructura simple: src/ , include/ , y Makefile en raíz

CC = gcc
CFLAGS = -Wall -Wextra -O2 -Iinclude
LDFLAGS = -lm

MPICC = mpicc
MPICFLAGS = -Wall -Wextra -O2 -Iinclude
MPILDFLAGS = -lm

# Directorios
SRC_DIR = src
INCLUDE_DIR = include

# Archivos fuente (todos en src/)
SOURCES = $(SRC_DIR)/main.c \
          $(SRC_DIR)/master.c \
          $(SRC_DIR)/worker.c \
          $(SRC_DIR)/fft.c \
          $(SRC_DIR)/crypto.c \
          $(SRC_DIR)/audiousb.c

# Objetos (se generan en el mismo directorio src/)
OBJECTS = $(SOURCES:.c=.o)

# Ejecutable final
TARGET = audio_dist

.PHONY: all clean run run4 run5

all: $(TARGET)

# Regla de enlazado
$(TARGET): $(OBJECTS)
	$(MPICC) $(MPICFLAGS) -o $@ $^ $(MPILDFLAGS)
	@echo "Ejecutable $(TARGET) creado"

# Regla de compilación para archivos .c
$(SRC_DIR)/%.o: $(SRC_DIR)/%.c $(INCLUDE_DIR)/*.h
	$(MPICC) $(MPICFLAGS) -c $< -o $@

# Limpiar
clean:
	rm -f $(SRC_DIR)/*.o $(TARGET)
	rm -f audio_plain.raw audio_encrypted.raw

# Ejecutar con 3 procesos (1 maestro + 2 trabajadores)
run: $(TARGET)
	mpirun -np 3 ./$(TARGET) cancion.wav

# Ejecutar con 4 procesos (1 maestro + 3 trabajadores)
run4: $(TARGET)
	mpirun -np 4 ./$(TARGET) cancion.wav

# Ejecutar con 5 procesos (1 maestro + 4 trabajadores)
run5: $(TARGET)
	mpirun -np 5 ./$(TARGET) cancion.wav

# Ejecutar con información detallada
run-verbose: $(TARGET)
	mpirun -np 4 --display-map --display-allocation ./$(TARGET) cancion.wav

# Verificar dependencias
check:
	@echo "Verificando instalación de MPI..."
	@which mpicc || echo "MPI no instalado. Ejecute: sudo apt install openmpi-bin openmpi-common libopenmpi-dev"
	@echo "Verificando archivos fuente..."
	@ls -la $(SRC_DIR)/*.c
	@echo "Verificando archivos de cabecera..."
	@ls -la $(INCLUDE_DIR)/*.h