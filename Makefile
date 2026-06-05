# ──────────────────────────────────────────────────────────────────────────────
# Makefile — Sistema de Análisis de Audio Distribuido con OpenMPI
#
# Targets:
#   make all        — Compila master y worker (con stub de libaudio)
#   make stub       — Genera libaudio.a desde libaudio_stub.c
#   make clean      — Elimina objetos y binarios
#   make run-3node  — Genera hostfile y script de clúster
# ──────────────────────────────────────────────────────────────────────────────

CC      = mpicc
CFLAGS  = -Wall -Wextra -O2 -std=c99 -Iinclude

SRC     = src
INC     = include

# Objetos compartidos entre master y worker
COMMON_OBJS = $(SRC)/crypto.o $(SRC)/fft.o

.PHONY: all stub clean run-3node

# ─── Orden: stub primero, luego los binarios ──────────────────────────────────
all: stub master worker

# ─── libaudio.a (stub para desarrollo sin hardware) ──────────────────────────
stub: libaudio.a

$(SRC)/libaudio_stub.o: $(SRC)/libaudio_stub.c $(INC)/common.h
	$(CC) $(CFLAGS) -c $< -o $@

libaudio.a: $(SRC)/libaudio_stub.o
	ar rcs $@ $<
	@echo "[Makefile] libaudio.a (stub) lista"

# ─── Módulos comunes ──────────────────────────────────────────────────────────
$(SRC)/crypto.o: $(SRC)/crypto.c $(INC)/crypto.h $(INC)/common.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC)/fft.o: $(SRC)/fft.c $(INC)/fft.h $(INC)/common.h
	$(CC) $(CFLAGS) -c $< -o $@

# ─── Nodo Maestro ─────────────────────────────────────────────────────────────
$(SRC)/master.o: $(SRC)/master.c $(INC)/common.h $(INC)/crypto.h $(INC)/fft.h
	$(CC) $(CFLAGS) -c $< -o $@

# NOTA: -lm y -laudio SIEMPRE al final (el linker resuelve de izquierda a derecha)
master: $(SRC)/master.o $(COMMON_OBJS) libaudio.a
	$(CC) -o $@ $(SRC)/master.o $(COMMON_OBJS) -L. -laudio -lm
	@echo "[Makefile] Binary 'master' listo"

# ─── Nodo Trabajador ──────────────────────────────────────────────────────────
$(SRC)/worker.o: $(SRC)/worker.c $(INC)/common.h $(INC)/crypto.h $(INC)/fft.h
	$(CC) $(CFLAGS) -c $< -o $@

worker: $(SRC)/worker.o $(COMMON_OBJS)
	$(CC) -o $@ $(SRC)/worker.o $(COMMON_OBJS) -lm
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
	@printf '#!/bin/bash\n# Uso: ./scripts/run_cluster.sh archivo.wav\n' > scripts/run_cluster.sh
	@printf '# Antes copiar binarios: scp master worker usuario@nodo2:~/\n' >> scripts/run_cluster.sh
	@printf 'mpirun --hostfile scripts/hostfile -np 4 ./master "$$1"\n' >> scripts/run_cluster.sh
	@chmod +x scripts/run_cluster.sh
	@echo "[Makefile] scripts/hostfile y scripts/run_cluster.sh generados"
	@echo "Edita scripts/hostfile con los hostnames/IPs reales de tu clúster"