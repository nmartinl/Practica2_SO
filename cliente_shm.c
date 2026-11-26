/**
 * @file cliente_shm.c
 * @brief Cliente CLI que accede al bloque de Memoria Compartida y coordina
 *        lecturas y escrituras mediante semáforos POSIX.
 */

#include "common.h"

void print_menu(void) {
    printf("\n=== CONSULTA DE MUSICA SPOTIFY (IPC - MEMORIA COMPARTIDA) ===\n");
    printf("1. Buscar canciones por Artista\n");
    printf("2. Buscar canciones por Género\n");
    printf("3. Buscar canción por Track ID\n");
    printf("4. Salir\n");
    printf("Seleccione una opción: ");
}

void print_record(const TrackRecord *tr) {
    printf("--------------------------------------------------\n");
    printf(" ID:           %s\n", tr->track_id);
    printf(" Título:       %s\n", tr->track_title);
    printf(" Artista:      %s\n", tr->artist_name);
    printf(" Género:       %s\n", tr->genre);
    printf(" Lanzamiento:  %s\n", tr->release_date);
    printf(" Explícita:    %s\n", tr->is_explicit ? "Sí" : "No");
    printf(" Duración:     %.2f s\n", tr->duration_ms / 1000.0);
}

int main(void) {
    printf("[CLIENTE-SHM] Iniciando proceso cliente (PID: %d, PPID: %d)...\n", getpid(), getppid());

    // Abrir segmento de memoria compartida existente
    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("[CLIENTE-SHM] Error al acceder a Memoria Compartida");
        printf("[CLIENTE-SHM] Asegúrese de haber ejecutado previamente el servidor_shm.\n");
        exit(EXIT_FAILURE);
    }

    SharedChannel *shm_ptr = (SharedChannel *)mmap(NULL, sizeof(SharedChannel), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED) {
        perror("[CLIENTE-SHM] Error en mmap");
        close(shm_fd);
        exit(EXIT_FAILURE);
    }

    // Abrir semáforos existentes
    sem_t *sem_req = sem_open(SEM_REQ, 0);
    sem_t *sem_res = sem_open(SEM_RES, 0);
    sem_t *sem_ack = sem_open(SEM_ACK, 0);

    if (sem_req == SEM_FAILED || sem_res == SEM_FAILED || sem_ack == SEM_FAILED) {
        perror("[CLIENTE-SHM] Error al abrir semáforos POSIX");
        munmap(shm_ptr, sizeof(SharedChannel));
        close(shm_fd);
        exit(EXIT_FAILURE);
    }

    while (1) {
        print_menu();
        int option;
        if (scanf("%d", &option) != 1) {
            while (getchar() != '\n');
            continue;
        }
        while (getchar() != '\n');

        if (option == 4) {
            printf("[CLIENTE-SHM] Finalizando ejecucion...\n");
            break;
        }

        if (option < 1 || option > 3) {
            printf("[CLIENTE-SHM] Opción inválida.\n");
            continue;
        }

        // Escribir petición en memoria compartida
        shm_ptr->req.type = (RequestType)option;
        printf("Ingrese el término de búsqueda: ");
        if (fgets(shm_ptr->req.param, sizeof(shm_ptr->req.param), stdin) != NULL) {
            shm_ptr->req.param[strcspn(shm_ptr->req.param, "\r\n")] = 0;
        }

        // Notificar al servidor que hay una petición lista
        sem_post(sem_req);

        // Protocolo de recepción secuencial bloque por bloque
        int count = 0;
        while (1) {
            sem_wait(sem_res); // Espera a que el servidor deposite una respuesta

            if (shm_ptr->res.status == RES_SUCCESS) {
                count++;
                print_record(&shm_ptr->res.record);
                sem_post(sem_ack); // Notifica al servidor que ya procesó la lectura
            } else if (shm_ptr->res.status == RES_NOT_FOUND) {
                printf("\n[RESULTADO] %s\n", shm_ptr->res.message);
                sem_post(sem_ack);
                break;
            } else if (shm_ptr->res.status == RES_END_OF_RESULTS) {
                printf("\n[RESULTADO] Búsqueda finalizada. Total encontrados: %d\n", count);
                sem_post(sem_ack);
                break;
            } else if (shm_ptr->res.status == RES_ERROR) {
                printf("\n[ERROR SERVIDOR] %s\n", shm_ptr->res.message);
                sem_post(sem_ack);
                break;
            }
        }
    }

    munmap(shm_ptr, sizeof(SharedChannel));
    close(shm_fd);
    sem_close(sem_req);
    sem_close(sem_res);
    sem_close(sem_ack);

    return 0;
}