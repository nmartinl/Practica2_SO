/**
 * @file cliente_fifo.c
 * @brief Cliente CLI para la versión FIFO. Presenta el menú interactivo,
 *        envía las solicitudes y muestra los resultados devueltos.
 */

#include "common.h"

void print_menu(void) {
    printf("\n=== CONSULTA DE MUSICA SPOTIFY (IPC - FIFO) ===\n");
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
    printf("[CLIENTE-FIFO] Iniciando proceso cliente (PID: %d, PPID: %d)...\n", getpid(), getppid());

    while (1) {
        print_menu();
        int option;
        if (scanf("%d", &option) != 1) {
            while (getchar() != '\n');
            continue;
        }
        while (getchar() != '\n'); // Limpiar buffer stdin

        if (option == 4) {
            printf("[CLIENTE-FIFO] Saliendo del programa...\n");
            break;
        }

        if (option < 1 || option > 3) {
            printf("[CLIENTE-FIFO] Opción inválida.\n");
            continue;
        }

        Request req;
        memset(&req, 0, sizeof(Request));
        req.type = (RequestType)option;

        printf("Ingrese el término de búsqueda: ");
        if (fgets(req.param, sizeof(req.param), stdin) != NULL) {
            req.param[strcspn(req.param, "\r\n")] = 0; // Remover salto de línea
        }

        // Apertura de FIFO_REQ (Bloquea si el servidor no ha abierto para lectura)
        int fd_req = open(FIFO_REQ, O_WRONLY);
        if (fd_req < 0) {
            perror("[CLIENTE-FIFO] Error al conectar con el servidor (Apertura FIFO_REQ)");
            printf("[CLIENTE-FIFO] Verifique que el proceso servidor esté ejecutándose.\n");
            continue;
        }

        int fd_res = open(FIFO_RES, O_RDONLY);
        if (fd_res < 0) {
            perror("[CLIENTE-FIFO] Error al abrir FIFO_RES para lectura");
            close(fd_req);
            continue;
        }

        // Enviar la estructura de petición
        if (write(fd_req, &req, sizeof(Request)) < 0) {
            perror("[CLIENTE-FIFO] Error al enviar la consulta");
            close(fd_req);
            close(fd_res);
            continue;
        }

        // Lectura continua de las respuestas
        Response res;
        int count = 0;
        while (read(fd_res, &res, sizeof(Response)) > 0) {
            if (res.status == RES_SUCCESS) {
                count++;
                print_record(&res.record);
            } else if (res.status == RES_NOT_FOUND) {
                printf("\n[RESULTADO] %s\n", res.message);
                break;
            } else if (res.status == RES_END_OF_RESULTS) {
                printf("\n[RESULTADO] Búsqueda finalizada. Total encontrados: %d\n", count);
                break;
            } else if (res.status == RES_ERROR) {
                printf("\n[ERROR SERVIDOR] %s\n", res.message);
                break;
            }
        }

        close(fd_req);
        close(fd_res);
    }

    return 0;
}