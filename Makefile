# Makefile - Práctica 1 Sistemas Operativos (UNAL)
# Compilación de Ejecutables FIFO y SHM

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lrt -lpthread

TARGETS = servidor_fifo cliente_fifo servidor_shm cliente_shm

all: $(TARGETS)

servidor_fifo: servidor_fifo.c common.h
	$(CC) $(CFLAGS) servidor_fifo.c -o servidor_fifo

cliente_fifo: cliente_fifo.c common.h
	$(CC) $(CFLAGS) cliente_fifo.c -o cliente_fifo

servidor_shm: servidor_shm.c common.h
	$(CC) $(CFLAGS) servidor_shm.c -o servidor_shm $(LDFLAGS)

cliente_shm: cliente_shm.c common.h
	$(CC) $(CFLAGS) cliente_shm.c -o cliente_shm $(LDFLAGS)

clean:
	rm -f $(TARGETS)
	rm -f /tmp/fifo_spotify_req /tmp/fifo_spotify_res
	rm -f /dev/shm/shm_spotify_channel
	rm -f /dev/shm/sem.sem_spotify_*

.PHONY: all clean