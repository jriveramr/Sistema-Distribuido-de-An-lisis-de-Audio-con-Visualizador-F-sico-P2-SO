# ──────────────────────────────────────────────────────────────────────────────
# Makefile — Sistema de Análisis de Audio Distribuido con OpenMPI
#
# Targets:
#   make all        — Compila master y worker (con stub de libaudio)
#   make clean      — Elimina objetos y binarios
#   make run-3node  — Genera hostfile y script de clúster
# ──────────────────────────────────────────────────────────────────────────────

CC     = mpicc
CFLAGS = -Wall -Wextra -O2 -std=c99 -Iinclude

SRC = src
INC = include

.PHONY: all clean run-3node

# ─── Regla principal ──────────────────────────────────────────────────────────
all: master worker

# ─── Compilación de objetos individuales ─────────────────────────────────────
$(SRC)/crypto.o: $(SRC)/crypto.c $(INC)/crypto.h $(INC)/common.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC)/fft.o: $(SRC)/fft.c $(INC)/fft.h $(INC)/common.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC)/libaudio_stub.o: $(SRC)/libaudio_stub.c $(INC)/common.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC)/master.o: $(SRC)/master.c $(INC)/common.h $(INC)/crypto.h $(INC)/fft.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC)/worker.o: $(SRC)/worker.c $(INC)/common.h $(INC)/crypto.h $(INC)/fft.h
	$(CC) $(CFLAGS) -c $< -o $@

# ─── Enlace: todos los .o explícitos, librerías AL FINAL ─────────────────────
#
# Se enlaza crypto.o y fft.o como objetos directos (NO como .a) para que
# GNU ld no los descarte en el primer pase de resolución de símbolos.
# libaudio_stub.o también se enlaza directo; -lm siempre al final.

master: $(SRC)/master.o $(SRC)/crypto.o $(SRC)/fft.o $(SRC)/libaudio_stub.o
	$(CC) -o $@ \
	    $(SRC)/master.o \
	    $(SRC)/crypto.o \
	    $(SRC)/fft.o \
	    $(SRC)/libaudio_stub.o \
	    -lm
	@echo "[Makefile] Binary 'master' listo"

worker: $(SRC)/worker.o $(SRC)/crypto.o $(SRC)/fft.o
	$(CC) -o $@ \
	    $(SRC)/worker.o \
	    $(SRC)/crypto.o \
	    $(SRC)/fft.o \
	    -lm
	@echo "[Makefile] Binary 'worker' listo"

# ─── Limpieza ─────────────────────────────────────────────────────────────────
clean:
	rm -f $(SRC)/*.o master worker libaudio.a
	rm -f audio_plain.raw audio_encrypted.raw
	@echo "[Makefile] Limpieza completa"

# ─── Clúster 3 nodos físicos ──────────────────────────────────────────────────
run-3node: all
	@mkdir -p scripts
	@printf "nodo1 slots=2\nnodo2 slots=1\nnodo3 slots=1\n" > scripts/hostfile
	@printf '#!/bin/bash\n# Uso: ./scripts/run_cluster.sh archivo.wav\n' \
	    > scripts/run_cluster.sh
	@printf 'mpirun --hostfile scripts/hostfile -np 4 ./master "$$1"\n' \
	    >> scripts/run_cluster.sh
	@chmod +x scripts/run_cluster.sh
	@echo "[Makefile] scripts/ generados. Edita hostfile con tus IPs."