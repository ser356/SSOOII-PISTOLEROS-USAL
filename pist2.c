#include <string.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include "pist2.h"


#define MIN_PIST 2
#define MAX_PIST 26
#define TAM_LIB 256

union semun {
    int val;
    struct semid_ds *buf;
    ushort *array;
} semunVar;

// CREA LOS SEMAFOROS Y EL BUZON
struct {
    int semaforoctl;// = -1;
    int semaforo1;// = -1;
    int semaforo2;// = -1;
    int semaforo3;// = -1;
    int semaforo4;
    int buzon;// = -1;
    int shmid;// = -1;
    char *p;
} datos;

void liberaIPC();
void INThandler(int );

int main(int argc, char const *argv[]) {

    // DECLARACIÓN DE VARIABLES
    int nPistoleros, ret, semilla, i, result, count, semVal, childPID=0;
    datos.semaforoctl=-1;
    datos.semaforo1=-1;
    datos.semaforo2=-1;
    datos.semaforo3=-1;
    datos.semaforo4=-1;
    datos.buzon=-1;
    datos.shmid=-1;
    datos.p = NULL;

    char victim, yo, menor = 0;
    char mensaje[100];
    struct msqid_ds *bufMSG;
    struct shmid_ds *bufSHM;

    struct msgbuf {
        long mtype;
        char mtext[7];
    } msg;

    signal(SIGINT, INThandler);

    // Gestiona los parámetros por línea de órdenes; error si nº de parámetros pasados incorrecto
    if (argc < 3 || argc > 4) {printf("\nNº de parametros pasados incorrecto (entre 2 o 3)\n\n"); return 100;}

    // Detectar si ha habido errores en el primer parámetro
    if (atoi(argv[1]) < MIN_PIST || atoi(argv[1]) > MAX_PIST) {
        printf("\nNº de pistoleros pasado fuera de rango (entre 2 y 26) -> %d\n\n", atoi(argv[1])); return 100;
    } else nPistoleros = atoi(argv[1]);

    // Detectar si ha habido errores en el segundo parámetro
    if (argv[2] < 0) {printf("\nParámetro de retención por debajo de cero\n\n"); return 100;}
    else ret = atoi(argv[2]);

    // Consigue la semilla para generar los números aleatorios
    if (argc == 4) semilla = atoi(argv[3]);
    else semilla = 0;



    // CREACIÓN DE MECANISMOS IPC
    datos.semaforoctl = semget(IPC_PRIVATE, 1, IPC_CREAT | 0600); // Semáforo reservado para libpist.a
    if(datos.semaforoctl==-1) kill(getpid(), SIGINT);

    // Semáforo de valor 1 para acceso único a zona de memoria
    datos.semaforo1 = semget(IPC_PRIVATE, 1, IPC_CREAT | 0600);
    if(datos.semaforo1==-1) kill(getpid(), SIGINT);

    // Semáforos de valor número de pistoleros vivos para barreras de control
    datos.semaforo2 = semget(IPC_PRIVATE, 1, IPC_CREAT | 0600);
    if(datos.semaforo2==-1) kill(getpid(), SIGINT);
    datos.semaforo3 = semget(IPC_PRIVATE, 1, IPC_CREAT | 0600);
    if(datos.semaforo3==-1) kill(getpid(), SIGINT);
    datos.semaforo4 = semget(IPC_PRIVATE, 1, IPC_CREAT | 0600);
    if(datos.semaforo4==-1) kill(getpid(), SIGINT);


    datos.buzon = msgget(IPC_PRIVATE, IPC_CREAT | 0600);
    datos.shmid = shmget(IPC_PRIVATE, 300*sizeof(char), IPC_CREAT | 0600); // Creación del id de memoria compartida
    datos.p = (char *) shmat(datos.shmid, (void*)0, 0); // Con NULL se asigna al primero disponible, con 0 es de escritura y lectura

    struct sembuf sops[3];
    semunVar.val=1;
    if(semctl(datos.semaforo1,0, SETVAL, semunVar)==-1)perror("Error semctl");
    semunVar.val = nPistoleros;
    if(semctl(datos.semaforo2, 0, SETVAL, semunVar)==-1)perror("Error semctl");
    if(semctl(datos.semaforo3, 0, SETVAL, semunVar)==-1)perror("Error semctl");
    if(semctl(datos.semaforo4, 0, SETVAL, semunVar)==-1)perror("Error semctl");

    // Wait y signal con valor 1
    sops[0].sem_num = 0;
    sops[0].sem_op = -1;
    sops[0].sem_flg = 0;
    sops[1].sem_num = 0;
    sops[1].sem_op = 1;
    sops[1].sem_flg = 0;

    // Wait-for-zero
    sops[2].sem_num = 0;
    sops[2].sem_op = 0;
    sops[2].sem_flg = 0;

    for (i=0; i<nPistoleros; i++) {
        datos.p[256+i] = 'A' + i;
    }

    datos.p[256+nPistoleros] = 0;    // Nº de pistoleros inicializado correctamente

    PIST_inicio(nPistoleros, ret, datos.semaforoctl, datos.p, semilla); // Inicia el programa

    for (i=0; i<nPistoleros; i++) {
        switch(fork()) {
            case -1:
                kill(getpid(), SIGINT); // Suicidio del padre
            case 0:
                // datos.p de memoria compartida
                if(semop(datos.semaforo1, &sops[0], 1))perror("Error semop"); //WAIT

                // llama PIST_nuevoPistolero
                if (PIST_nuevoPistolero(datos.p[256+datos.p[256+nPistoleros]]) == -1) {kill(getppid(), SIGINT); return 100;}
                // Guarda cada proceso en memoria local su propio caracter asignado
                yo = datos.p[256+datos.p[256+nPistoleros]];
                // Añade un pistolero vivo a la cuenta
                datos.p[256+nPistoleros] += 1;

                if(semop(datos.semaforo1, &sops[1], 1)==-1) perror("Error semop"); //SIGNAL

                while (1) { // Bucle de rondas

                    menor = 0;
                    result = -1;
                    count = 0;

                    if(semop(datos.semaforo4, &sops[0], 1)==-1) perror("Error semop");
                    if(semop(datos.semaforo4, &sops[2], 1)==-1) perror("Error semop"); // wait-for-zero

                    // Bucle for para encontrar al menor valor que queda al comienzo de cada ronda
                    for (i=0; i<nPistoleros; i++) {if (datos.p[256+i] > menor) menor = datos.p[256+i];}

                    // Si solo queda un pistolero vivo, matarlo
                    if (datos.p[256+nPistoleros] == 1) {
                        msg.mtype = 1;
                        msg.mtext[0] = getpid();
                        if(msgsnd(datos.buzon, &msg, sizeof(msg.mtext), 0)==-1){perror("Error msgsnd"); kill(getppid(), SIGINT);}
                    }

                    victim = PIST_vIctima();
                    if (victim == '@') {kill(getppid(), SIGINT);}

                    // 1 Sem W0 - Esperar a estar todos iniciados correctamente
                    if (yo == menor) {
                        semunVar.val = datos.p[256+nPistoleros];
                        if(semctl(datos.semaforo3, 0, SETVAL, semunVar)==-1)perror("Error semctl");
                    }
                    if(semop(datos.semaforo2, &sops[0], 1)==-1)perror("Error semop"); // Primero resta 1 al semáforo
                    if(semop(datos.semaforo2, &sops[2], 1)==-1)perror("Error semop"); // luego espera a que el valor llegue a cero

                    // Si es incorrecta matar el programa entero
                    msg.mtype = victim;
                    if (msgsnd(datos.buzon, &msg, sizeof(msg.mtext), 0) == -1) {perror("Error msgsnd"); kill(getppid(), SIGINT);}
                    PIST_disparar(victim);

                    // 2 Sem W0 - Esperar a que todos hayan elegido victima y disparado
                    if (yo == menor) {
                        semunVar.val = datos.p[256+nPistoleros];
                        if(semctl(datos.semaforo4, 0, SETVAL, semunVar)==-1)perror("Error semctl");
                    }
                    if(semop(datos.semaforo3, &sops[0], 1)==-1)perror("Error semop");
                    if(semop(datos.semaforo3, &sops[2], 1)==-1)perror("Error semop");

                    // Aqui usamos un bucle while porque puede haber más de un mensaje
                    // para el mismo proceso, si es asi, eliminamos ese mensaje.

                    do {
                        if((result = msgrcv(datos.buzon, &msg, sizeof(msg.mtext), yo, IPC_NOWAIT))==-1 && errno != ENOMSG) {perror("Error msgrcv");raise(SIGINT);}
                        if (result != -1) count += 1;
                    } while(result != -1);

                    // Entra el proceso que ha recibido uno o más mensajes de morir
                    if (count > 0) {
                        PIST_morirme(); // Morirse en la pantalla
                        if(semop(datos.semaforo1, &sops[0], 1)==-1) perror("Error semop");

                        // Pone su posición en el array a 0
                        datos.p[256+yo-'A'] = 0;
                        datos.p[256+nPistoleros] -= 1;
                        // Restar uno al semáforo que va a hacer W0 por cada proceso que muere->
                        // semaforo controla victimas zerocount se incrementa hasta que w0 llega
                        semunVar.val = datos.p[256+nPistoleros];
                        if(semctl(datos.semaforo2, 0, SETVAL, semunVar)==-1)perror("Error semctl");
                        if(semop(datos.semaforo4, &sops[0], 1)==-1)perror("Error semop"); // Wait al semáforo para comprobar que
                                                             // o han muerto o pasado todos.
                        if(semop(datos.semaforo1, &sops[1], 1)==-1)perror("Error semop"); // Signal para salir si el proceso muere
                        return 0;
                    }
                }
        }
    }

    for (i=0; i<nPistoleros; i++) {
        childPID = wait(NULL);
    }
    if (msgrcv(datos.buzon, &msg, 7, 1, IPC_NOWAIT) == -1) {
        liberaIPC();
        PIST_fin();
        return 0;
    } else {
        liberaIPC();
        PIST_fin();
        return childPID;
    }

}

void INThandler(int sig) {
    liberaIPC();
    PIST_fin();
    exit(100);
}

void liberaIPC() {
    semctl(datos.semaforoctl, 0, IPC_RMID, semunVar);
    semctl(datos.semaforo1, 0, IPC_RMID, semunVar); // Una muerte por semáforo
    semctl(datos.semaforo2, 0, IPC_RMID, semunVar);
    semctl(datos.semaforo3,0, IPC_RMID, semunVar);
    semctl(datos.semaforo4,0, IPC_RMID, semunVar);
    msgctl(datos.buzon, IPC_RMID, 0); // Una muerte para el buzón
    shmctl(datos.shmid, IPC_RMID, 0); // Muerte de la memoria compartida
}
