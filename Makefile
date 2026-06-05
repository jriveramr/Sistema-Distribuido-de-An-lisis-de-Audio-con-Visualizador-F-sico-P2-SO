# ──────────────────────────────────────────────────────────────────────────────
# Makefile — Sistema de Análisis de Audio Distribuido con OpenMPI
#
# Targets principales:
#   make all        — Compila maestro y trabajador (con stub de libaudio)
#   make stub       — Genera libaudio.a desde el stub
#   make clean      — Elimina objetos y binarios
#   make run        — Ejemplo de ejecución con 4 procesos (1M + 3W)
#   make run-3node  — Hostfile de 3 nodos físicos
# ──────────────────────────────────────────────────────────────────────────────

CC       = mpicc
CFLAGS   = -Wall -Wextra -O2 -std=c99 -Iinclude
LDFLAGS  = -lm -L. -laudio

SRC_DIR  = src
INC_DIR  = include

# ── Fuentes comunes (compiladas para ambos binarios) ──────────────────────────
COMMON_SRCS = $(SRC_DIR)/crypto.c \
              $(SRC_DIR)/fft.c

COMMON_OBJS = $(COMMON_SRCS:.c=.o)

# ── Targets ───────────────────────────────────────────────────────────────────
.PHONY: all stub clean run run-3node

all: stub master worker

# ── Stub de libaudio.a (usar si no tienes la librería real) ──────────────────
stub: libaudio.a

libaudio.a: $(SRC_DIR)/libaudio_stub.o
	ar rcs $@ $<
	@echo "[Makefile] libaudio.a (stub) creada"

$(SRC_DIR)/libaudio_stub.o: $(SRC_DIR)/libaudio_stub.c $(INC_DIR)/common.h
	$(CC) $(CFLAGS) -c $< -o $@

# ── Nodo Maestro ──────────────────────────────────────────────────────────────
master: $(SRC_DIR)/master.o $(COMMON_OBJS) libaudio.a
	$(CC) $(CFLAGS) -o $@ $(SRC_DIR)/master.o $(COMMON_OBJS) $(LDFLAGS)
	@echo "[Makefile] Binary 'master' generado"

$(SRC_DIR)/master.o: $(SRC_DIR)/master.c $(INC_DIR)/common.h \
                     $(INC_DIR)/crypto.h $(INC_DIR)/fft.h
	$(CC) $(CFLAGS) -c $< -o $@

# ── Nodo Trabajador ───────────────────────────────────────────────────────────
worker: $(SRC_DIR)/worker.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $(SRC_DIR)/worker.o $(COMMON_OBJS) -lm
	@echo "[Makefile] Binary 'worker' generado"

$(SRC_DIR)/worker.o: $(SRC_DIR)/worker.c $(INC_DIR)/common.h \
                     $(INC_DIR)/crypto.h $(INC_DIR)/fft.h
	$(CC) $(CFLAGS) -c $< -o $@

# ── Módulos comunes ───────────────────────────────────────────────────────────
$(SRC_DIR)/crypto.o: $(SRC_DIR)/crypto.c $(INC_DIR)/crypto.h $(INC_DIR)/common.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/fft.o: $(SRC_DIR)/fft.c $(INC_DIR)/fft.h $(INC_DIR)/common.h
	$(CC) $(CFLAGS) -c $< -o $@

# ── Limpieza ──────────────────────────────────────────────────────────────────
clean:
	rm -f $(SRC_DIR)/*.o master worker libaudio.a
	rm -f audio_plain.raw audio_encrypted.raw
	@echo "[Makefile] Limpieza completada"

# ── Ejecución local (1 maestro + 3 trabajadores = 4 procesos) ────────────────
run: all
	@echo "Uso: mpirun -np 4 --host localhost:4 ./master <archivo.wav>"
	@echo "     El proceso rango 0 = maestro, rangos 1-3 = trabajadores"
	@echo ""
	@echo "IMPORTANTE: master y worker deben ser el MISMO binario en un"
	@echo "clúster real. Ver scripts/run_cluster.sh para el modo correcto."

# ── Ejecución en clúster de 3 nodos físicos ──────────────────────────────────
run-3node: all
	@echo "Generando scripts/hostfile y scripts/run_cluster.sh ..."
	@mkdir -p scripts
	@echo "nodo1 slots=1" >  scripts/hostfile
	@echo "nodo2 slots=2" >> scripts/hostfile
	@echo "nodo3 slots=1" >> scripts/hostfile
	@echo "#!/bin/bash"                                        >  scripts/run_cluster.sh
	@echo "# Copiar binarios a los nodos antes de ejecutar:"  >> scripts/run_cluster.sh
	@echo "# scp master worker nodo2:~/ && scp master worker nodo3:~/" >> scripts/run_cluster.sh
	@echo "mpirun --hostfile scripts/hostfile \\"              >> scripts/run_cluster.sh
	@echo "       -np 4 \\"                                    >> scripts/run_cluster.sh
	@echo "       ./master \$$1"                               >> scripts/run_cluster.sh
	@chmod +x scripts/run_cluster.sh
	@echo "Listo. Edita scripts/hostfile con los nombres/IPs de tus nodos."
	@echo "Luego ejecuta: ./scripts/run_cluster.sh <archivo.wav>"