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
#define MAS_SCD 11234

#define PRO_TO_SCHED 20
#define SCHED_TO_PRO 30
#define PAGE_FAULT_HANDLED 12
#define TERMINATED 13

typedef struct pro_msg{
    long mtype;
    int id;
}pro_msg;

typedef struct mmu_to_sch{
    long mtype;
    char buf[1];
}mmu_to_sch;

void sem_wait(int);
void sem_signal(int);
int k;
int main(int argc, char *argv[]) {
    k = -1;
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <mq1> <mq2> <totalnumberofprocesses>\n", argv[0]);
        sleep(10);
        exit(EXIT_FAILURE);
    }
    k = atoi(argv[3]);
    printf("Total No. of Process received = %d\n", k);
    int mq1 = atoi(argv[1]);
    int mq2 = atoi(argv[2]);
    int master_sched = semget(MAS_SCD, 1, IPC_CREAT|0777);

    int terminated=0;
    while(1){
        if (terminated == k){
            break;
        }
        pro_msg msg_rcv;
        int l = msgrcv(mq1, &msg_rcv, sizeof(msg_rcv), PRO_TO_SCHED, 0);
        int id = msg_rcv.id;
        printf("scheduled %d\n", id);
        pro_msg msg_send;
        msg_send.mtype = SCHED_TO_PRO+id;
        msg_send.id = id;
        msgsnd(mq1, &msg_send, sizeof(msg_send), 0);

        mmu_to_sch mmu_msg_rcv;
        l = msgrcv(mq2, &mmu_msg_rcv, sizeof(mmu_msg_rcv), 0, 0);
        if (mmu_msg_rcv.mtype == PAGE_FAULT_HANDLED){
            msg_send.mtype = PRO_TO_SCHED;
            msg_send.id = id;
            msgsnd(mq1, &msg_send, sizeof(msg_send), 0);
            printf("\t\tPGF\n");
        }
        else if (mmu_msg_rcv.mtype == TERMINATED){
            printf("\t\tTERM\n");
            terminated ++;
        }
        else
		{
			perror("Wrong message from mmu\n");
		}

        if (terminated == k){
            break;
        }
    }
    printf("scheduler exitting\n");
    sem_signal(master_sched);
    
    return 0;
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