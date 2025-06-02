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
const int total_keywords =
        sizeof(palabras_reservadas) / sizeof(palabras_reservadas[0]);

/* ===============  variables globales ================== */
char (*infos)[BUFFER];        
int n_hilos;             

long lin_efe = 0, key_word = 0, comenta = 0;
int  fin = 0;

/* ===============  sincronización ====================== */
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_barrier_t barr_lectura;
pthread_barrier_t barr_analizador;

/* =========================================================
 *          FUNCIONES AUXILIARES POR LÍNEA
 * =========================================================*/
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

/* =========================================================
 *                 TRABAJO DE CADA HILO
 * =========================================================*/
void* trabajador(void *arg){
    int id = *((int*)arg);

    while (1) {
        pthread_barrier_wait(&barr_lectura);
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

        pthread_barrier_wait(&barr_analizador);
    }
    pthread_barrier_wait(&barr_analizador);   
    pthread_exit(0);
}

/* =========================================================
 *                 PRODUCTOR  (hilo principal)
 * =========================================================*/
void leer_archivo(const char *nombre){
    FILE *file = fopen(nombre,"r");
    if (!file) { 
        perror("fopen"); 
        exit(EXIT_FAILURE); 
    }

    while (1) {
        int leidas = 0;
        for (int i = 0; i < n_hilos; i++) {
            if (fgets(infos[i],BUFFER,file)) 
                leidas++;
            else 
                infos[i][0]='\0';
        }

        pthread_mutex_lock(&lock);
        fin = (leidas==0);
        pthread_mutex_unlock(&lock);

        pthread_barrier_wait(&barr_lectura);
        pthread_barrier_wait(&barr_analizador);

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

    pthread_barrier_init(&barr_lectura,  NULL, n_hilos+1);
    pthread_barrier_init(&barr_analizador,NULL, n_hilos+1);

    pthread_t *vec_h = (pthread_t *) malloc(n_hilos * sizeof(pthread_t));
    if (vec_h == NULL) {
        perror("No se pudo reservar memoria para vec_h");
        exit(EXIT_FAILURE);
    }
    //Crear hilos
    for (int i = 0; i < n_hilos; i++) {
        int* index = malloc(sizeof(int));
        *index = i;
        pthread_create(&vec_h[i], NULL, trabajador, (void*) index);
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
    pthread_barrier_destroy(&barr_lectura);
    pthread_barrier_destroy(&barr_analizador);
    pthread_mutex_destroy(&lock);
    free(infos); 
    free(vec_h);
    return 0;
}
