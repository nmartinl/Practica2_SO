/**
 * @file servidor_fifo.c
 * @brief Servidor IPC que utiliza FIFO (tuberías nombradas) para atender consultas
 *        sobre el archivo CSV sin cargarlo completamente en RAM.
 */

#include "common.h"
#include <ctype.h>

static volatile int keep_running = 1;

/**
 * @brief Manejador de señales para captura de SIGINT (Ctrl+C).
 */
void handle_sigint(int sig) {
    (void)sig;
    keep_running = 0;
    printf("\n[SERVIDOR-FIFO] Recibida señal de terminación. Cerrando servidor...\n");
}

/**
 * @brief Convierte una cadena a minúsculas para comparaciones no sensibles a mayúsculas.
 */
void to_lowercase(char *str) {
    for (; *str; ++str) *str = (char)tolower(*str);
}

/**
 * @brief Procesa el dataset CSV secuencialmente fila por fila.
 * @param req Petición recibida del cliente.
 * @param fd_res Descriptor de archivo abierto hacia la FIFO de respuesta.
 */
void process_query(const Request *req, int fd_res) {
    FILE *fp = fopen(CSV_PATH, "r");
    if (!fp) {
        perror("[SERVIDOR-FIFO] Error al abrir el dataset CSV");
        Response res = {.status = RES_ERROR};
        snprintf(res.message, sizeof(res.message), "No se pudo abrir el archivo de datos");
        write(fd_res, &res, sizeof(Response));
        return;
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t read_bytes;

    // Omitir la primera línea (cabecera del CSV)
    if ((read_bytes = getline(&line, &len, fp)) == -1) {
        free(line);
        fclose(fp);
        return;
    }

    int match_count = 0;
    char param_lower[MAX_STR];
    strncpy(param_lower, req->param, sizeof(param_lower));
    param_lower[sizeof(param_lower) - 1] = '\0';
    to_lowercase(param_lower);

    // Lectura secuencial línea a línea (Cumple: dataset permanece en disco)
    while ((read_bytes = getline(&line, &len, fp)) != -1) {
        // Copia temporal de la línea para parsing
        char line_copy[MAX_LINE];
        strncpy(line_copy, line, sizeof(line_copy));
        line_copy[sizeof(line_copy) - 1] = '\0';

        // Estructura de campos CSV
        TrackRecord tr;
        memset(&tr, 0, sizeof(TrackRecord));

        char *token;
        char *rest = line_copy;

        if ((token = strtok_r(rest, ",\n\r", &rest))) strncpy(tr.track_id, token, sizeof(tr.track_id));
        if ((token = strtok_r(NULL, ",\n\r", &rest))) strncpy(tr.track_title, token, sizeof(tr.track_title));
        if ((token = strtok_r(NULL, ",\n\r", &rest))) strncpy(tr.artist_name, token, sizeof(tr.artist_name));
        if ((token = strtok_r(NULL, ",\n\r", &rest))) strncpy(tr.genre, token, sizeof(tr.genre));
        if ((token = strtok_r(NULL, ",\n\r", &rest))) strncpy(tr.release_date, token, sizeof(tr.release_date));
        if ((token = strtok_r(NULL, ",\n\r", &rest))) tr.is_explicit = atoi(token);
        if ((token = strtok_r(NULL, ",\n\r", &rest))) tr.duration_ms = atol(token);

        int is_match = 0;
        char temp_field[MAX_STR];

        if (req->type == REQ_BY_ARTIST) {
            strncpy(temp_field, tr.artist_name, sizeof(temp_field));
            to_lowercase(temp_field);
            if (strstr(temp_field, param_lower) != NULL) is_match = 1;
        } else if (req->type == REQ_BY_GENRE) {
            strncpy(temp_field, tr.genre, sizeof(temp_field));
            to_lowercase(temp_field);
            if (strstr(temp_field, param_lower) != NULL) is_match = 1;
        } else if (req->type == REQ_BY_ID) {
            if (strcmp(tr.track_id, req->param) == 0) is_match = 1;
        }

        if (is_match) {
            match_count++;
            Response res;
            memset(&res, 0, sizeof(Response));
            res.status = RES_SUCCESS;
            res.record = tr;

            // Escritura bloqueante sobre la FIFO de respuesta
            if (write(fd_res, &res, sizeof(Response)) < 0) {
                perror("[SERVIDOR-FIFO] Error escribiendo en FIFO respuesta");
                break;
            }

            if (req->type == REQ_BY_ID) break; // ID es único
        }
    }

    free(line); // Liberación explícita de memoria dinámica usada por getline
    fclose(fp); // Cierre de archivo CSV

    // Envío del fin de transmisión o mensaje de no encontrado
    Response final_res;
    memset(&final_res, 0, sizeof(Response));
    if (match_count == 0) {
        final_res.status = RES_NOT_FOUND;
        snprintf(final_res.message, sizeof(final_res.message), "No se encontraron registros para la consulta.");
    } else {
        final_res.status = RES_END_OF_RESULTS;
    }
    write(fd_res, &final_res, sizeof(Response));
}

int main(void) {
    signal(SIGINT, handle_sigint);

    printf("[SERVIDOR-FIFO] Iniciando servidor (PID: %d, PPID: %d)...\n", getpid(), getppid());

    // Crear FIFOs en el sistema de archivos si no existen
    if (mkfifo(FIFO_REQ, 0666) < 0 && errno != EEXIST) {
        perror("[SERVIDOR-FIFO] Error creando FIFO_REQ");
        exit(EXIT_FAILURE);
    }
    if (mkfifo(FIFO_RES, 0666) < 0 && errno != EEXIST) {
        perror("[SERVIDOR-FIFO] Error creando FIFO_RES");
        unlink(FIFO_REQ);
        exit(EXIT_FAILURE);
    }

    printf("[SERVIDOR-FIFO] Esperando conexiones de cliente...\n");

    while (keep_running) {
        // Abrir FIFO de petición (bloquea hasta que un cliente abra para escritura)
        int fd_req = open(FIFO_REQ, O_RDONLY);
        if (fd_req < 0) {
            if (errno == EINTR) break;
            perror("[SERVIDOR-FIFO] Error abriendo FIFO_REQ");
            continue;
        }

        // Abrir FIFO de respuesta
        int fd_res = open(FIFO_RES, O_WRONLY);
        if (fd_res < 0) {
            perror("[SERVIDOR-FIFO] Error abriendo FIFO_RES");
            close(fd_req);
            continue;
        }

        Request req;
        ssize_t bytes_read = read(fd_req, &req, sizeof(Request));
        if (bytes_read > 0) {
            if (req.type == REQ_EXIT) {
                printf("[SERVIDOR-FIFO] Petición de cierre recibida. Finalizando ciclo.\n");
                close(fd_req);
                close(fd_res);
                break;
            }
            printf("[SERVIDOR-FIFO] Procesando petición tipo %d con parámetro: '%s'\n", req.type, req.param);
            process_query(&req, fd_res);
        }

        close(fd_req);
        close(fd_res);
    }

    // Limpieza de FIFOs creadas
    unlink(FIFO_REQ);
    unlink(FIFO_RES);
    printf("[SERVIDOR-FIFO] Servidor finalizado y recursos liberados.\n");
    return 0;
}