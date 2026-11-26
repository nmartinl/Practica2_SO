/**
 * @file common.h
 * @brief Definiciones de estructuras, protocolos y constantes compartidas
 *        entre los procesos cliente y servidor (FIFO y Memoria Compartida).
 */

#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <signal.h>
#include <errno.h>

// Rutas para tuberías nombradas (FIFO)
#define FIFO_REQ "/tmp/fifo_spotify_req"
#define FIFO_RES "/tmp/fifo_spotify_res"

// Nombres para recursos POSIX de Memoria Compartida y Semáforos
#define SHM_NAME "/shm_spotify_channel"
#define SEM_REQ  "/sem_spotify_req"
#define SEM_RES  "/sem_spotify_res"
#define SEM_ACK  "/sem_spotify_ack"

#define MAX_STR 256
#define MAX_LINE 2048
#define CSV_PATH "track_metadata.csv"

/**
 * @brief Tipos de peticiones soportadas en el protocolo IPC.
 */
typedef enum {
    REQ_BY_ARTIST = 1,   // Búsqueda por nombre de artista (subcadena)
    REQ_BY_GENRE  = 2,   // Búsqueda por género musical
    REQ_BY_ID     = 3,   // Búsqueda por ID exacto de pista
    REQ_EXIT      = 4    // Orden de desconexión / terminación
} RequestType;

/**
 * @brief Estados de respuesta en el protocolo IPC.
 */
typedef enum {
    RES_SUCCESS        = 0,  // Registro encontrado y enviado
    RES_NOT_FOUND      = 1,  // Consulta no arrojó resultados
    RES_END_OF_RESULTS = 2,  // Fin del envío de resultados
    RES_ERROR          = 3   // Error procesando la petición
} ResponseStatus;

/**
 * @brief Estructura que representa una fila del dataset CSV (track_metadata.csv).
 */
typedef struct {
    char track_id[64];
    char track_title[128];
    char artist_name[128];
    char genre[64];
    char release_date[32];
    int is_explicit;
    long duration_ms;
} TrackRecord;

/**
 * @brief Mensaje de Petición enviado desde el Cliente al Servidor.
 */
typedef struct {
    RequestType type;
    char param[MAX_STR];
} Request;

/**
 * @brief Mensaje de Respuesta enviado desde el Servidor al Cliente.
 */
typedef struct {
    ResponseStatus status;
    TrackRecord record;
    char message[MAX_STR];
} Response;

/**
 * @brief Estructura mapeada en Memoria Compartida (Parte B).
 */
typedef struct {
    Request req;
    Response res;
} SharedChannel;

#endif // COMMON_H