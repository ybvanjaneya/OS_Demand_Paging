#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/types.h>
#include <time.h>
#include <limits.h>
#include <string.h>


// Define constants for shared memory and message queues
#define SM1_KEY 6789
#define SM2_KEY 7890
#define SM3_KEY 8900
#define MQ1_KEY 3456
#define MQ2_KEY 4567
#define MQ3_KEY 5678
#define MAS_SCD 11234

typedef struct page_table_entry{
    int frame;
    int valid;
    int last_access;
}page_table_entry;

// Function to initialize data structures and create child processes
int initialize(int k, int m, int f);
void signal_handle();
void sem_wait(int);
void sem_signal(int);
int k, m, f;
int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <total_processes> <max_pages_per_process> <total_frames>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    k = m = f = -1;
    k = atoi(argv[1]); // Total number of processes
    m = atoi(argv[2]); // Maximum number of pages per process
    f = atoi(argv[3]); // Total number of frames

    int master_sched = semget(MAS_SCD, 1, IPC_CREAT|0777);
    semctl(master_sched, 0, SETVAL, 0);
    // Initialize data structures and create child processes
    int mmu = initialize(k, m, f);

    printf("master waiting\n");
    sem_wait(master_sched);
    //kill(mmu, SIGINT);
    sleep(1);
    signal_handle();

    return 0;
}

int initialize(int k, int m, int f) {
    // Create and initialize shared memory for page table
    int shm1 = shmget(SM1_KEY, k*m*sizeof(page_table_entry), IPC_CREAT | 0777);
    // Create and initialize shared memory for free frame list
    int shm2 = shmget(SM2_KEY, sizeof(int) * f, IPC_CREAT | 0777);
    // process to page number mapping
    int shm3 = shmget(SM3_KEY, sizeof(int)*k, IPC_CREAT | 0777);
    // Create message queues
    int mq1 = msgget(MQ1_KEY, IPC_CREAT | 0777);
    int mq2 = msgget(MQ2_KEY, IPC_CREAT | 0777);
    int mq3 = msgget(MQ3_KEY, IPC_CREAT | 0777);

    if (shm1 == -1 || shm2 == -1 || shm3 == -1 || mq1 == -1 || mq2 == -1 || mq3 == -1) {
        perror("Error creating shared memory or message queues");
        signal_handle();
    }
    
    page_table_entry* global_table = (page_table_entry*) shmat(shm1, 0, 0);
    
    for(int i=0; i<k; i++){
        for(int j=0; j<m; j++){
            global_table[j + (m*i)].frame = -1;
            global_table[j + (m*i)].valid = 0;
            global_table[j + (m*i)].last_access = 0;
        }
    }
    
    shmdt(global_table);
    
    int * free_frame_list = (int*)shmat(shm2, 0, 0);
    
    for(int i=0; i<f; i++){
        free_frame_list[i] = 1;
    }
    
    shmdt(free_frame_list);
    printf("Data structures created succesfully\n");

    sleep(1);
    char ak[10]={'\0'};
    char am[10]={'\0'};
    char af[10]={'\0'};
    char amq1[10]={'\0'};
    char amq2[10]={'\0'};
    char amq3[10]={'\0'};
    char ashm1[10]={'\0'};
    char ashm2[10]={'\0'};
    char ashm3[10]={'\0'};

    sprintf(ak, "%d", k);
    sprintf(am, "%d", m);
    sprintf(af, "%d", f);
    sprintf(amq1, "%d", mq1);
    sprintf(amq2, "%d", mq2);
    sprintf(amq3, "%d", mq3);
    sprintf(ashm1, "%d", shm1);
    sprintf(ashm2, "%d", shm2);
    sprintf(ashm3, "%d", shm3);
    // Create scheduler
    // Fork process and exec scheduler
    printf("master:%d\n", getpid());
    int sch = fork();
    if (sch == 0){
        printf("creating sched %d\n", getpid());

        execlp("xterm", "xterm", "-e", "./sched",amq1, amq2, ak, NULL);
        perror("execlp");
        exit(0);
    }
    else if (sch < 0){
        perror("fork\n");
    }
    // Create MMU
    // Fork process and exec MMU
    int mmu = fork();
    if (mmu == 0){
        printf("mmu created%d\n", getpid());
        execlp("xterm","xterm","-e", "./mmu", amq2, amq3, ashm1, ashm2, ashm3, am, af, ak, NULL);
        //execlp("./mmu", "./mmu", amq2, amq3, ashm1, ashm2, ashm3, am, af, ak, NULL);
        printf("execlp-mmu%d\n", getpid());
        exit(0);
    }
    else if (mmu < 0){
        perror("fork\n");
    }

    // Create k processes at fixed intervals 250000usec
    int* process_pages = shmat(shm3, NULL, 0);
    srand(time(NULL));
    for(int i=0; i<k; i++){
        int num_pages = rand()% m + 1;
        process_pages[i] = num_pages;

        int ref_len = rand() % (8 * num_pages +1) + (2* num_pages);
        int* ref_string = (int*)malloc(ref_len* sizeof(int));
        printf("p: %d, n:%d, ref_len:%d\n", i, num_pages, ref_len);

        for (int j = 0; j < ref_len; j++) {
            // Generate a random page number for the reference string
            int page_num;
            int rand_val = rand() % 100;
            if (rand_val < 5) { // 5% probability of generating an illegal page number
                page_num = rand() % num_pages + m; // Illegal page number >= m
            } else if (rand_val < 20 && m!=num_pages) { // 15% probability of generating an invalid but legal page number
                page_num = rand() % (m - num_pages) + num_pages; // Invalid but legal page number >= num_pages and < m
            } else {
                page_num = rand() % num_pages; // Valid page number within the range of assigned pages
            }
            ref_string[j] = page_num;
        }

        char ref_string_str[1024] = "";
        for (int j = 0; j < ref_len; j++) {
            char num_str[10]={'\0'};
            sprintf(num_str, "%d", ref_string[j]);
            strcat(ref_string_str, num_str);
            if (j != ref_len - 1) {
                strcat(ref_string_str, ",");
            }
        }
        //printf("ref:%s\n", ref_string_str);

        int pid=fork();
        if (pid == 0){
            char a1[10]={'\0'};
            char a2[10]={'\0'};
            char a3[10]={'\0'};
            sprintf(a1, "%d", mq1);
            sprintf(a2, "%d", mq3);
            sprintf(a3, "%d", i);
            char a4[10]={'\0'};
            sprintf(a4, "%d", num_pages);
            execlp("xterm","xterm","-e", "./process", a1, a2, a3, a4, ref_string_str, NULL);
            perror("execlp-process");
        }
        else if (pid < 0){
            perror("fork");
        }
        usleep(250000);
        free(ref_string);
    }
    shmdt(process_pages);

    return mmu;
}


void signal_handle(){
    int shm1 = shmget(SM1_KEY, k*m*sizeof(page_table_entry), IPC_CREAT | 0777);
    // Create and initialize shared memory for free frame list
    int shm2 = shmget(SM2_KEY, sizeof(int) * f, IPC_CREAT | 0777);
    // process to page number mapping
    int shm3 = shmget(SM3_KEY, sizeof(int)*k, IPC_CREAT | 0777);
    // Create message queues
    int mq1 = msgget(MQ1_KEY, IPC_CREAT | 0777);
    int mq2 = msgget(MQ2_KEY, IPC_CREAT | 0777);
    int mq3 = msgget(MQ3_KEY, IPC_CREAT | 0777);

    int master_sched = semget(MAS_SCD, 1, IPC_CREAT|0777);

    shmctl(shm1, IPC_RMID, 0);
    shmctl(shm2, IPC_RMID, 0);
    shmctl(shm3, IPC_RMID, 0);
    msgctl(mq1, IPC_RMID, 0);
    msgctl(mq2, IPC_RMID, 0);
    msgctl(mq3, IPC_RMID, 0);

    semctl(master_sched, 0, IPC_RMID, 0);

    exit(0);
}

void sem_wait(int semid){
    struct sembuf sem_op;
    sem_op.sem_flg = 0;
    sem_op.sem_num = 0;
    sem_op.sem_op = -1;
    semop(semid, &sem_op, 1);
}
void sem_signal(int semid){
    struct sembuf sem_op;
    sem_op.sem_flg = 0;
    sem_op.sem_num = 0;
    sem_op.sem_op = 1;
    semop(semid, &sem_op, 1);
}