#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include <stdint.h>
#include <time.h>

#define BUFFER 256

/* ------------ tabla de palabras reservadas ------------ */
const char* palabras_reservadas[] = {
    "int","long","float","double","char","const","void",
    "if","else","switch","case","return",
    "for","while","do","break","continue",
    "sizeof","struct","typedef","unsigned","#include"
};
const int total_keywords = sizeof(palabras_reservadas) / sizeof(palabras_reservadas[0]);

/* ===============  variables globales ================== */
char (*infos)[BUFFER];
int n_hilos;

long lin_efe = 0, key_word = 0, comenta = 0;
int fin = 0;

/* ===============  sincronización ====================== */
int *trabajo_disponible;  // bandera: 1 si hay trabajo asignado
int *trabajo_hecho;       // bandera: 1 si el hilo terminó su trabajo

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;


/* =========================================================
 *          FUNCIONES AUXILIARES POR LÍNEA
 * =========================================================*/
int cuenta_linea_efectiva(const char *l) {
    while (isspace((unsigned char)*l)) ++l;
    if (!*l || !strncmp(l,"//",2) || !strncmp(l,"/*",2) || !strncmp(l,"#include",8) || strcmp(l,"{")==0 || strcmp(l,"}")==0)
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

/* =========================================================
 *                 TRABAJO DE CADA HILO
 * =========================================================*/

void* trabajador(void *arg){
    int id = *((int*)arg);
    free(arg);

    while (1) {
        int procesar = 0;

        while (1) {
            pthread_mutex_lock(&mutex);
            if (fin && !trabajo_disponible[id]) {
                pthread_mutex_unlock(&mutex);
                pthread_exit(0);
            }

            if (trabajo_disponible[id]) {
                trabajo_disponible[id] = 0; // lo tomo
                procesar = 1;
                pthread_mutex_unlock(&mutex);
                break;
            }
            pthread_mutex_unlock(&mutex);
        }

        if (procesar) {
            int le = cuenta_linea_efectiva(infos[id]);
            int kw = cuenta_palabras_reservadas(infos[id]);
            int cm = cuenta_comentarios(infos[id]);

            pthread_mutex_lock(&mutex);
            lin_efe += le;
            key_word += kw;
            comenta  += cm;
            trabajo_hecho[id] = 1;
            pthread_mutex_unlock(&mutex);
        }
    }
}

/* =========================================================
 *                 PRODUCTOR  (hilo principal)
 * =========================================================*/

void leer_archivo(const char *nombre){
    FILE *file = fopen(nombre, "r");
    if (!file) { perror("fopen"); exit(EXIT_FAILURE); }

    while (1) {
        int leidas = 0;

        pthread_mutex_lock(&mutex);
        for (int i = 0; i < n_hilos; i++) {
            if (fgets(infos[i], BUFFER, file)) {
                trabajo_disponible[i] = 1;
                trabajo_hecho[i] = 0;
                leidas++;
            } else {
                trabajo_disponible[i] = 0;
                trabajo_hecho[i] = 1; // ya está hecho (nada que procesar)
                infos[i][0] = '\0';
            }
        }
        pthread_mutex_unlock(&mutex);

        if (leidas == 0) break;

        // Esperar a que todos terminen
        int todos_listos;
        do {
            todos_listos = 1;
            pthread_mutex_lock(&mutex);
            for (int i = 0; i < n_hilos; i++) {
                if (!trabajo_hecho[i]) {
                    todos_listos = 0;
                    break;
                }
            }
            pthread_mutex_unlock(&mutex);
        } while (!todos_listos);
    }

    pthread_mutex_lock(&mutex);
    fin = 1;
    pthread_mutex_unlock(&mutex);
}

int main(int argc,char **argv){
    if (argc < 3){
        printf("Uso: %s <archivo> <hilos>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    n_hilos = atoi(argv[2]);
    if (n_hilos <= 0){ 
        printf("Número de hilos inválido\n");
        exit(EXIT_FAILURE);
    }

    infos = malloc(n_hilos * sizeof *infos);
    trabajo_disponible = calloc(n_hilos, sizeof(int));
    trabajo_hecho = calloc(n_hilos, sizeof(int));
    pthread_t *vec_h = malloc(n_hilos * sizeof(pthread_t));
    if (!infos || !trabajo_disponible || !trabajo_hecho || !vec_h) {
        perror("malloc");
        exit(EXIT_FAILURE);
    }

    //Crear hilos
    for (int i = 0; i < n_hilos; i++) {
        int* index = malloc(sizeof(int));
        *index = i;
        pthread_create(&vec_h[i], NULL, trabajador, index);
    }

    /*Medicion*/
    struct timespec start, end;
    clock_gettime(1, &start);

    /* produce datos */
    leer_archivo(argv[1]);

    for (int i = 0; i < n_hilos; i++) {
        pthread_join(vec_h[i], NULL);
    }

    /* resultados */
    clock_gettime(1, &end);
    double tiempo = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec)/1e9;
    printf("Tiempo total: %.3f segundos\n", tiempo);
    printf("Latencia: %.6f segundo/líneas\n", tiempo/(lin_efe + comenta));
    printf("Throughput: %.2f líneas/segundo\n\n", (lin_efe + comenta) / tiempo);

    printf("Líneas efectivas : %ld\n", lin_efe);
    printf("Palabras clave   : %ld\n", key_word);
    printf("Comentarios      : %ld\n", comenta);

    /* limpieza */
    free(infos); free(vec_h);
    free(trabajo_disponible); free(trabajo_hecho);
    pthread_mutex_destroy(&mutex);
    return 0;
}
