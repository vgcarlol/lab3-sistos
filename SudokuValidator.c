// SudokuValidator_base.c
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <omp.h>

#define SIZE 9

// Declaración global de la grilla
int sudoku[SIZE][SIZE];

// Función para validar filas
int validateRow() {
    omp_set_nested(1); 
    int valid = 1;
    #pragma omp parallel for schedule(dynamic) reduction(&&: valid)
    for (int i = 0; i < SIZE; i++) {
        int seen[10] = {0};
        for (int j = 0; j < SIZE; j++) {
            int num = sudoku[i][j];
            if (num < 1 || num > 9 || seen[num]) {
                valid = 0;
                break;
            }
            seen[num] = 1;
        }
    }
    
    return valid;
}


// Función para validar columnas
int validateColumn() {
    int valid = 1;
    for (int j = 0; j < SIZE; j++) {
        int seen[10] = {0};
        for (int i = 0; i < SIZE; i++) {
            int num = sudoku[i][j];
            if (num < 1 || num > 9 || seen[num]) {
                valid = 0;
                break;
            }
            seen[num] = 1;
        }
        if (!valid)
            break;
    }
    return valid;
}

// Función para validar subarreglos 3x3
int validateSubgrid(int startRow, int startCol) {
    int seen[10] = {0};
    for (int i = startRow; i < startRow + 3; i++) {
        for (int j = startCol; j < startCol + 3; j++) {
            int num = sudoku[i][j];
            if (num < 1 || num > 9 || seen[num])
                return 0;
            seen[num] = 1;
        }
    }
    return 1;
}

// Función asignable al pthread para validar columnas
void *threadValidateColumn(void *arg) {
    int res = validateColumn();
    pid_t tid = syscall(SYS_gettid);
    printf("Validación de columnas en thread, ID: %d, resultado: %d\n", tid, res);
    pthread_exit(0);
}

int main(int argc, char *argv[]) {
    // Limitar el número de threads a 1
    omp_set_num_threads(1);


    if(argc < 2) {
        fprintf(stderr, "Uso: %s <archivo_sudoku>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    // Abrir el archivo
    int fd = open(argv[1], O_RDONLY);
    if(fd < 0) {
        perror("open");
        exit(EXIT_FAILURE);
    }
    
    // Obtener tamaño del archivo y mapearlo en memoria
    struct stat st;
    if(fstat(fd, &st) < 0) {
        perror("fstat");
        exit(EXIT_FAILURE);
    }
    
    char *file_content = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if(file_content == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    close(fd);
    
    // Copiar los 81 dígitos a la grilla
    int index = 0;
    for (int i = 0; i < SIZE; i++) {
        for (int j = 0; j < SIZE; j++) {
            // Saltar caracteres no numéricos
            while(file_content[index] < '0' || file_content[index] > '9')
                index++;
            sudoku[i][j] = file_content[index] - '0';
            index++;
        }
    }
    munmap(file_content, st.st_size);
    
    // Validar subarreglos (en posiciones [0,0], [3,3], [6,6])
    int subgrid_valid = 1;
    int positions[3] = {0, 3, 6};
    for(int i = 0; i < 3; i++) {
        if(!validateSubgrid(positions[i], positions[i])) {
            subgrid_valid = 0;
            break;
        }
    }
    
    // Primer fork: En el hijo se ejecuta ps -p <pid> -lLf
    pid_t pid = fork();
    if(pid == 0) {
         char parent_pid_str[10];
         sprintf(parent_pid_str, "%d", getppid());
         execlp("ps", "ps", "-p", parent_pid_str, "-lLf", NULL);
         perror("execlp");
         exit(EXIT_FAILURE);
    }
    
    // Proceso padre: Crear pthread para validación de columnas
    pthread_t tid;
    if(pthread_create(&tid, NULL, threadValidateColumn, NULL) != 0) {
         perror("pthread_create");
         exit(EXIT_FAILURE);
    }
    pthread_join(tid, NULL);
    wait(NULL); // Esperar a que termine el hijo del fork anterior

    // Validar filas
    int row_valid = validateRow();
    
    // Combinar resultados (en este ejemplo, se asume que la validación es correcta si filas y subarreglos son válidos)
    int sudoku_valid = (row_valid && subgrid_valid);
    
    if(sudoku_valid)
         printf("La solución del sudoku es válida.\n");
    else
         printf("La solución del sudoku es inválida.\n");
    
    // Segundo fork: Ejecutar ps de nuevo para comparar LWP’s
    pid = fork();
    if(pid == 0) {
         char parent_pid_str[10];
         sprintf(parent_pid_str, "%d", getppid());
         execlp("ps", "ps", "-p", parent_pid_str, "-lLf", NULL);
         perror("execlp");
         exit(EXIT_FAILURE);
    }
    wait(NULL);
    
    return 0;
}
