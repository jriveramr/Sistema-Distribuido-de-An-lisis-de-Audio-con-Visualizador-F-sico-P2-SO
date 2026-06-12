# ──────────────────────────────────────────────────────────────────────────────
# Makefile — Sistema de Análisis de Audio Distribuido con OpenMPI
#
# Genera UN SOLO binario (audio_dist) para todos los procesos MPI.
# El proceso rango 0 actúa de maestro; los rangos >= 1 de trabajadores.
# El maestro escribe al driver /dev/audiousb directamente con write().
#
# Uso:
#   make              — Compila audio_dist
#   make clean        — Elimina objetos y binario
#   make run-3node    — Genera hostfile y script de clúster
# ──────────────────────────────────────────────────────────────────────────────

CC     = mpicc
CFLAGS = -Wall -Wextra -O2 -std=c99 -Iinclude

SRC = src
INC = include

.PHONY: all clean run-3node

all: audio_dist

# ─── Compilación de cada unidad de traducción ─────────────────────────────────
$(SRC)/main.o: $(SRC)/main.c $(INC)/common.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC)/master.o: $(SRC)/master.c $(INC)/common.h $(INC)/crypto.h \
                 $(INC)/fft.h $(INC)/sysmon.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC)/worker.o: $(SRC)/worker.c $(INC)/common.h $(INC)/crypto.h \
                 $(INC)/fft.h $(INC)/sysmon.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC)/crypto.o: $(SRC)/crypto.c $(INC)/crypto.h $(INC)/common.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC)/fft.o: $(SRC)/fft.c $(INC)/fft.h $(INC)/common.h
	$(CC) $(CFLAGS) -c $< -o $@

# ─── Enlace: sin libaudio.a — el maestro usa open()/write()/close() ──────────
audio_dist: $(SRC)/main.o \
            $(SRC)/master.o \
            $(SRC)/worker.o \
            $(SRC)/crypto.o \
            $(SRC)/fft.o
	$(CC) -o $@ $^ -lm
	@echo "[Makefile] Binary 'audio_dist' listo"
	@echo "Ejecutar: mpirun -np 4 ./audio_dist archivo.wav"

# ─── Limpieza ─────────────────────────────────────────────────────────────────
clean:
	rm -f $(SRC)/*.o audio_dist libaudio.a
	rm -f audio_plain.raw audio_encrypted.raw
	@echo "[Makefile] Limpieza completa"

# ─── Clúster 3 nodos físicos ──────────────────────────────────────────────────
run-3node: all
	@mkdir -p scripts
	@printf "nodo1 slots=2\nnodo2 slots=1\nnodo3 slots=1\n" > scripts/hostfile
	@printf '#!/bin/bash\n# Uso: ./scripts/run_cluster.sh archivo.wav\n' \
	    > scripts/run_cluster.sh
	@printf '# Copiar antes: scp audio_dist usuario@nodo2:~/ usuario@nodo3:~/\n' \
	    >> scripts/run_cluster.sh
	@printf 'mpirun --hostfile scripts/hostfile -np 4 ./audio_dist "$$1"\n' \
	    >> scripts/run_cluster.sh
	@chmod +x scripts/run_cluster.sh
	@echo "[Makefile] Edita scripts/hostfile con tus IPs y ejecuta:"
	@echo "           ./scripts/run_cluster.sh archivo.wav"