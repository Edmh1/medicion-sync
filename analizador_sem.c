#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <stdint.h>
#include <semaphore.h>
#include <time.h>


#define BUFFER 256

const char* palabras_reservadas[] = {
    "int","long","float","double","char","const","void",
    "if","else","switch","case","return",
    "for","while","do","break","continue",
    "sizeof","struct","typedef","unsigned","#include"
};
const int total_keywords =
        sizeof(palabras_reservadas) / sizeof(palabras_reservadas[0]);

char (*infos)[BUFFER];
int n_hilos;

long lin_efe = 0, key_word = 0, comenta = 0;
int fin = 0;

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* === SEMÁFOROS === */
sem_t *sem_lineas_listas;     // Productor avisa a cada hilo que ya hay línea
sem_t *sem_listos;            // Cada hilo avisa que leyó su línea
sem_t *sem_analizados;        // Cada hilo avisa que terminó análisis
sem_t sem_productor;          // Productor espera a que todos analicen

int cuenta_linea_efectiva(const char *l){
    while (isspace((unsigned char)*l)) ++l;
    if (!*l || !strncmp(l,"//",2) || !strncmp(l,"/*",2) || !strncmp(l,"#include",8) || strcmp(l, "{") == 0 || strcmp(l, "}") == 0)
        return 0;

    size_t len = strlen(l);
    while (len > 0 && isspace((unsigned char)l[len-1])) --len;
    int termina_pycoma = (len > 0 && l[len-1] == ';');

    int es_comp = strstr(l,"==")||strstr(l,"!=")||strstr(l,"<=")||strstr(l,">=")||
                  strstr(l,"<")||strstr(l,">");
    return (termina_pycoma || es_comp);
}

int cuenta_palabras_reservadas(const char *orig){
    char copia[BUFFER];
    strncpy(copia, orig, BUFFER-1);
    copia[BUFFER-1]='\0';
    char *saveptr=NULL, *tok=NULL;
    int local=0;
    for(tok=strtok_r(copia," \t\n;(){}=<>+-*^%!&|",&saveptr); tok; tok=strtok_r(NULL," \t\n;(){}=<>+-*^%!&|",&saveptr)){
        for(int j=0;j<total_keywords;++j){
            if(!strcmp(tok,palabras_reservadas[j])) {
                ++local;
                break;
            }
        }
    }
    return local;
}

int cuenta_comentarios(const char *l){
    int en_bloque=0, local=0;
    const char *p=l;
    while(*p){
        if(!en_bloque && !strncmp(p,"//",2)){
            ++local;
            break;
        }
        else if(!en_bloque && !strncmp(p,"/*",2)){
            en_bloque=1;
            ++local;
            p+=2;
            continue;
        }
        else if(en_bloque && !strncmp(p,"*/",2)){
            en_bloque=0;
            p+=2;
            continue;
        }
        ++p;
    }
    return local;
}

void* trabajador(void *arg){
    int id = *((int*)arg);
    free(arg);

    while (1) {
        sem_wait(&sem_lineas_listas[id]); // Esperar línea del productor
        if (fin) break;

        int le = 0, kw = 0, cm = 0;

        if (infos[id][0] != '\0') {
            le = cuenta_linea_efectiva(infos[id]);
            kw = cuenta_palabras_reservadas(infos[id]);
            cm = cuenta_comentarios(infos[id]);
        }

        pthread_mutex_lock(&lock);
        lin_efe += le;
        key_word += kw;
        comenta  += cm;
        pthread_mutex_unlock(&lock);

        sem_post(&sem_listos[id]);       // Avisar que se leyó
        sem_post(&sem_analizados[id]);   // Avisar que se analizó
    }

    sem_post(&sem_listos[id]);          // Última señal para desbloquear productor
    sem_post(&sem_analizados[id]);      // Última señal para desbloquear productor
    pthread_exit(0);
}

void leer_archivo(const char *nombre){
    FILE *file = fopen(nombre,"r");
    if (!file) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    while (1) {
        int leidas = 0;
        for (int i = 0; i < n_hilos; i++) {
            if (fgets(infos[i], BUFFER, file))
                leidas++;
            else
                infos[i][0] = '\0';
        }

        pthread_mutex_lock(&lock);
        fin = (leidas == 0);
        pthread_mutex_unlock(&lock);

        // Avisar a todos los hilos que hay líneas nuevas
        for (int i = 0; i < n_hilos; i++) {
            sem_post(&sem_lineas_listas[i]);
        }

        // Esperar a que todos los hilos terminen
        for (int i = 0; i < n_hilos; i++) {
            sem_wait(&sem_analizados[i]);
        }

        if (fin) break;
    }
    fclose(file);
}

int main(int argc,char **argv){
    if (argc < 3){
        printf("Debe proporcionar el nombre del archivo y el numero de hilos\n");
        exit(EXIT_FAILURE);
    }
    n_hilos = atoi(argv[2]);
    if (n_hilos <= 0){
        printf("Numero de hilos invalido\n");
        exit(EXIT_FAILURE);
    }

    infos = malloc(n_hilos * sizeof *infos);
    if (!infos) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    sem_lineas_listas = malloc(n_hilos * sizeof(sem_t));
    sem_listos        = malloc(n_hilos * sizeof(sem_t));
    sem_analizados    = malloc(n_hilos * sizeof(sem_t));

    for (int i = 0; i < n_hilos; i++) {
        sem_init(&sem_lineas_listas[i], 0, 0);
        sem_init(&sem_listos[i],        0, 0);
        sem_init(&sem_analizados[i],    0, 0);
    }

    pthread_t *vec_h = malloc(n_hilos * sizeof(pthread_t));
    for (int i = 0; i < n_hilos; i++) {
        int* index = malloc(sizeof(int));
        *index = i;
        pthread_create(&vec_h[i], NULL, trabajador, (void*) index);
    }

    /*Medicion*/
    struct timespec start, end;
    clock_gettime(1, &start);
    leer_archivo(argv[1]);

    for (int i = 0; i < n_hilos; i++) {
        pthread_join(vec_h[i], NULL);
    }

    /*Resultados*/
    clock_gettime(1, &end);
    double tiempo = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec)/1e9;
    printf("Tiempo total: %.3f segundos\n", tiempo);
    printf("Latencia: %.6f segundo/líneas\n", tiempo/(lin_efe + comenta));
    printf("Throughput: %.2f líneas/segundo\n\n", (lin_efe + comenta) / tiempo);

    printf("Líneas efectivas : %ld\n", lin_efe);
    printf("Palabras clave   : %ld\n", key_word);
    printf("Comentarios      : %ld\n", comenta);

    // limpieza
    for (int i = 0; i < n_hilos; i++) {
        sem_destroy(&sem_lineas_listas[i]);
        sem_destroy(&sem_listos[i]);
        sem_destroy(&sem_analizados[i]);
    }

    free(sem_lineas_listas);
    free(sem_listos);
    free(sem_analizados);
    pthread_mutex_destroy(&lock);
    free(infos);
    free(vec_h);

    return 0;
}
