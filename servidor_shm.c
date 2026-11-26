/**
 * @file servidor_shm.c
 * @brief Servidor que utiliza Memoria Compartida POSIX (shm_open, mmap)
 *        y Semáforos explícitos (sem_open) para la sincronización y comunicación.
 */

#include "common.h"
#include <ctype.h>

static volatile int keep_running = 1;
static int shm_fd = -1;
static SharedChannel *shm_ptr = MAP_FAILED;
static sem_t *sem_req = SEM_FAILED;
static sem_t *sem_res = SEM_FAILED;
static sem_t *sem_ack = SEM_FAILED;

void cleanup_resources(void) {
    if (shm_ptr != MAP_FAILED) munmap(shm_ptr, sizeof(SharedChannel));
    if (shm_fd != -1) close(shm_fd);
    if (sem_req != SEM_FAILED) sem_close(sem_req);
    if (sem_res != SEM_FAILED) sem_close(sem_res);
    if (sem_ack != SEM_FAILED) sem_close(sem_ack);

    shm_unlink(SHM_NAME);
    sem_unlink(SEM_REQ);
    sem_unlink(SEM_RES);
    sem_unlink(SEM_ACK);
    printf("[SERVIDOR-SHM] Recursos IPC eliminados correctamente.\n");
}

void handle_sigint(int sig) {
    (void)sig;
    keep_running = 0;
    printf("\n[SERVIDOR-SHM] Interrupción recibida. Limpiando y cerrando...\n");
    cleanup_resources();
    exit(0);
}

void to_lowercase(char *str) {
    for (; *str; ++str) *str = (char)tolower(*str);
}

void process_shm_query(const Request *req) {
    FILE *fp = fopen(CSV_PATH, "r");
    if (!fp) {
        shm_ptr->res.status = RES_ERROR;
        snprintf(shm_ptr->res.message, sizeof(shm_ptr->res.message), "Error al abrir CSV");
        sem_post(sem_res);
        sem_wait(sem_ack);
        return;
    }

    char *line = NULL;
    size_t len = 0;
    ssize_t read_bytes;

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

    while ((read_bytes = getline(&line, &len, fp)) != -1) {
        char line_copy[MAX_LINE];
        strncpy(line_copy, line, sizeof(line_copy));
        line_copy[sizeof(line_copy) - 1] = '\0';

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
            shm_ptr->res.status = RES_SUCCESS;
            shm_ptr->res.record = tr;

            // Sincronización estricta: Servidor publica resultado y espera ACK del cliente
            sem_post(sem_res);
            sem_wait(sem_ack);

            if (req->type == REQ_BY_ID) break;
        }
    }

    free(line);
    fclose(fp);

    // Notificar fin o resultado no encontrado
    if (match_count == 0) {
        shm_ptr->res.status = RES_NOT_FOUND;
        snprintf(shm_ptr->res.message, sizeof(shm_ptr->res.message), "No se encontraron coincidencia.");
    } else {
        shm_ptr->res.status = RES_END_OF_RESULTS;
    }
    sem_post(sem_res);
    sem_wait(sem_ack);
}

int main(void) {
    signal(SIGINT, handle_sigint);
    cleanup_resources(); // Limpiar residuos previos si los hubiera

    printf("[SERVIDOR-SHM] Iniciando servidor (PID: %d, PPID: %d)...\n", getpid(), getppid());

    // 1. Crear e inicializar segmento de memoria compartida
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("[SERVIDOR-SHM] Error en shm_open");
        exit(EXIT_FAILURE);
    }

    if (ftruncate(shm_fd, sizeof(SharedChannel)) < 0) {
        perror("[SERVIDOR-SHM] Error en ftruncate");
        cleanup_resources();
        exit(EXIT_FAILURE);
    }

    shm_ptr = (SharedChannel *)mmap(NULL, sizeof(SharedChannel), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED) {
        perror("[SERVIDOR-SHM] Error en mmap");
        cleanup_resources();
        exit(EXIT_FAILURE);
    }

    // 2. Crear Semáforos POSIX
    sem_req = sem_open(SEM_REQ, O_CREAT, 0666, 0);
    sem_res = sem_open(SEM_RES, O_CREAT, 0666, 0);
    sem_ack = sem_open(SEM_ACK, O_CREAT, 0666, 0);

    if (sem_req == SEM_FAILED || sem_res == SEM_FAILED || sem_ack == SEM_FAILED) {
        perror("[SERVIDOR-SHM] Error abriendo semáforos POSIX");
        cleanup_resources();
        exit(EXIT_FAILURE);
    }

    printf("[SERVIDOR-SHM] Memoria Compartida y Semáforos inicializados con éxito.\n");

    while (keep_running) {
        printf("[SERVIDOR-SHM] Esperando peticiones en sem_req...\n");
        if (sem_wait(sem_req) < 0) {
            if (errno == EINTR) break;
            perror("[SERVIDOR-SHM] Error en sem_wait(sem_req)");
            continue;
        }

        if (shm_ptr->req.type == REQ_EXIT) {
            printf("[SERVIDOR-SHM] Solicitud de cierre recibida.\n");
            break;
        }

        printf("[SERVIDOR-SHM] Procesando consulta tipo %d: '%s'\n", shm_ptr->req.type, shm_ptr->req.param);
        process_shm_query(&shm_ptr->req);
    }

    cleanup_resources();
    return 0;
}