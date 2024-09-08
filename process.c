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
#define PRO_TO_SCHED 20
#define SCHED_TO_PRO 30
#define PRO_TO_MMU 100
#define MMU_TO_PRO 110

typedef struct pro_msg{
    long mtype;
    int id;
}pro_msg;

typedef struct mmu_to_pro{
    long mtype;
    int frame;
}mmu_to_pro;
typedef struct pro_to_mmu{
    long mtype;
    int id;
    int page;
}pro_to_mmu;

int n_pages;
int main(int argc, char *argv[]) {
    n_pages=-1;
    if (argc != 6) {
        fprintf(stderr, "Usage: %s <mq1> <mq3> <id> <max_pages> <ref_str>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int id = atoi(argv[3]);
    int mq1 = atoi(argv[1]);
    int mq3 = atoi(argv[2]);
    int max_pages = atoi(argv[4]);
    char* ref_str = argv[5];
    printf("id:%d,ref:%s\n", id, ref_str);
    sleep(1);

    pro_msg msg_send;
    msg_send.mtype = PRO_TO_SCHED;
    msg_send.id = id;
    msgsnd(mq1, &msg_send, sizeof(msg_send), 0);

    pro_msg msg_recv;
    msgrcv(mq1, &msg_recv, sizeof(msg_recv), SCHED_TO_PRO+id, 0);

    printf("process %d executing\n", id);

    int c=0;
    const char s[2] = ",";
	char *token;
	token = strtok(ref_str, s);
    n_pages=0;
    int* pages = (int*)malloc(max_pages* sizeof(int));
	while ( token != NULL )
	{
		pages[n_pages] = atoi(token);
        //printf("|%d|", pages[n_pages]);
		n_pages++;
		token = strtok(NULL, s);
	}
    //printf("\n");

    
    while(c < n_pages){
        printf("page %d\n", pages[c]);
        pro_to_mmu ms;
        ms.id = id;
        ms.page = pages[c];
        ms.mtype = PRO_TO_MMU;
        msgsnd(mq3, &ms, sizeof(ms), 0);

        mmu_to_pro mr;
        msgrcv(mq3, &mr, sizeof(mr), MMU_TO_PRO+id, 0);
        if (mr.frame >= 0){
            printf("\tframe %d\n", mr.frame);
            c++;
        }
        else if (mr.frame == -1){
            printf("\tPGF %d\n", pages[c]);
        }
        else if (mr.frame == -2){
            printf("\tinvalid pg ref terminating %d\n", id);
            sleep(1);
            exit(1);
        }
        else {
            printf("**invalid frame %d\n", mr.frame);
            c++;
        }
        sleep(3);
    }

    printf("process %d terminating...\n", id);
    pro_to_mmu ems;
    ems.id = id;
    ems.mtype = PRO_TO_MMU;
    ems.page = -9;
    msgsnd(mq3, &ems, sizeof(ems), 0);

    free(pages);
    return 0;
}
