# Práctica 1: Procesos, llamadas al sistema e IPC en Linux
**Universidad Nacional de Colombia - Ingeniería de Sistemas**
**Asignatura:** Sistemas Operativos
**Integrantes** 
- Luis Bustos
- Antonio Garay
- Nicolas Martín
- Jhon Rodriguez

---

## 1. Descripción del Dataset
Se utilizó el dataset de Spotify **`track_metadata.csv`**, el cual contiene registros de canciones estructurados en los siguientes campos:
- `track_id`: Código alfanumérico identificador de la canción.
- `track_title`: Título de la pista musical.
- `artist_name`: Nombre del artista o grupo.
- `genre`: Género musical asociado.
- `release_date`: Fecha de lanzamiento (AAAA-MM-DD).
- `is_explicit`: Indicador binario (1 si contiene lenguaje explícito, 0 en caso contrario).
- `duration_ms`: Duración de la canción en milisegundos.

---

## 2. Observaciones con herramientas de Linux

Entorno: CachyOS Linux, ejecución desde terminal (fish). Las observaciones corresponden
a la versión FIFO con el servidor en reposo (sin cliente conectado). Cada herramienta
se ejecutó en una corrida distinta, por eso los PID no coinciden entre ellas.

### Observación 1: estado de un proceso bloqueado esperando IPC (`ps` + `lsof`)

Comando:
```bash
ps -o pid,ppid,stat,wchan:25,cmd -C servidor_fifo
lsof -p $(pgrep servidor_fifo)
```

Resultado relevante:
```
    PID    PPID STAT WCHAN               CMD
1000303   21562 S<+  wait_for_partner    ./servidor_fifo
```

- `STAT = S`: sueño interruptible. El proceso no consume CPU, está dormido en el
  kernel y no hace espera activa.
- `WCHAN = wait_for_partner`: función del kernel donde duerme el proceso. Es la
  espera de la FIFO hasta que otro proceso abra el extremo contrario (rendezvous).
- `lsof` solo muestra los descriptores 0, 1 y 2 (`/dev/pts/1`) y no muestra ningún
  descriptor para `/tmp/fifo_spotify_req`. Mientras `open()` no retorna, el kernel
  no ha entregado ningún descriptor. (Un descriptor adicional apuntaba a un archivo
  de VS Code, heredado del entorno de la terminal; no pertenece al programa.)

Conclusión: el servidor en reposo está bloqueado dentro de
`open(FIFO_REQ, O_RDONLY)`, esperando que un cliente abra la FIFO para escritura.

### Observación 2: secuencia de llamadas al sistema (`strace`)

Comando:
```bash
strace ./servidor_fifo
```

Secuencia relevante tras el arranque:
```
rt_sigaction(SIGINT, {sa_handler=..., sa_flags=SA_RESTORER|SA_INTERRUPT|SA_NODEFER|SA_RESETHAND}, ...) = 0
getppid()  = 1157755
getpid()   = 1157760
write(1, "[SERVIDOR-FIFO] Iniciando servidor ...", 68) = 68
mknodat(AT_FDCWD, "/tmp/fifo_spotify_req", S_IFIFO|0666) = -1 EEXIST
mknodat(AT_FDCWD, "/tmp/fifo_spotify_res", S_IFIFO|0666) = -1 EEXIST
write(1, "[SERVIDOR-FIFO] Esperando conexiones ...", 51) = 51
openat(AT_FDCWD, "/tmp/fifo_spotify_req", O_RDONLY
```

- `mkfifo()` se traduce en la syscall `mknodat` con `S_IFIFO`. Retornó `EEXIST`
  porque las FIFO quedaron de una ejecución anterior; el código tolera ese error y
  reutiliza el recurso.
- La última línea queda sin `= resultado`: el proceso está dentro de la syscall
  `openat`, dormido. Coincide con `WCHAN = wait_for_partner` de la observación 1.
- Al redimensionar la terminal llegó `SIGWINCH`: el kernel interrumpió el `openat`
  (`ERESTARTSYS`) y lo reinició de forma transparente, sin que el programa lo notara.
  Es una señal sin manejador, cuya acción por defecto es ignorarla.

---

## 3. Instrucciones de Compilación y Ejecución

### Compilación
Para compilar ambas versiones (FIFO y Memoria Compartida), ejecute en la terminal:
```bash
make
