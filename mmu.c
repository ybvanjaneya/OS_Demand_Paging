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
#define PRO_TO_MMU 100
#define MMU_TO_PRO 110
#define INVALID_PAGE_REF (-2)
#define PAGEFAULT (-1)
#define PROCESS_OVER (-9)
#define PAGE_FAULT_HANDLED 12
#define TERMINATED 13

typedef struct page_table_entry{
    int frame;
    int valid;
    int last_access;
}page_table_entry;

typedef struct mmu_to_sch{
    long mtype;
    char buf[1];
}mmu_to_sch;
typedef struct mmu_to_pro{
    long mtype;
    int frame;
}mmu_to_pro;
typedef struct pro_to_mmu{
    long mtype;
    int id;
    int page;
}pro_to_mmu;

int count;
int flag;
int terminated;
int* Per_pro_pgf;
int* Per_pro_invalid;
FILE*  file;
int k;
int find_victim_page(const page_table_entry *page_table,int,int, int num_pages);
int find_free_frame(const int *, int );
void sigint_handle();
int main(int argc, char *argv[]) {
    count = 0;
    flag=0;
    terminated = 0;
    if (argc != 9) {
        fprintf(stderr, "Usage: %s <mq2> <mq3> <shm1> <shm2> <shm3> <m> <f> <k>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    int mq2 = atoi(argv[1]);
    int mq3 = atoi(argv[2]);
    int shm1 = atoi(argv[3]);
    int shm2 = atoi(argv[4]);
    int shm3 = atoi(argv[5]);
    int m = atoi(argv[6]);
    int f = atoi(argv[7]);
    k = atoi(argv[8]);

    Per_pro_pgf = (int*)malloc(k*sizeof(int));
    Per_pro_invalid = (int*)malloc(k*sizeof(int));
    for(int i=0; i<k; i++){
        Per_pro_pgf[i] = 0;
        Per_pro_invalid[i] = 0;
    }
    page_table_entry* global_table = (page_table_entry*) shmat(shm1, 0, 0);
    int * free_frame_list = (int*)shmat(shm2, 0, 0);
    int* process_pages = shmat(shm3, NULL, 0);
    printf("mmu executing %d\n", getpid());
    flag=1;
    file = fopen("result.txt", "w");
    // recv page number and return frame number
    if (terminated >= k){
        flag = 0;
    }
    while(flag==1){
        fflush(file);
        if (terminated >= k){
            flag = 0;
        }
        pro_to_mmu mr;
        msgrcv(mq3, &mr, sizeof(mr), PRO_TO_MMU, 0);
        int id = mr.id;
        if ( mr.page == PROCESS_OVER){
            printf("mmu: process %d over\n", id);
            mmu_to_sch mshd;
            mshd.mtype = TERMINATED;
            terminated++;
            msgsnd(mq2, &mshd, sizeof(mshd), 0);
            continue;
        }
        int pageno =  mr.page;
        count++;
        printf("Page ref- (time:%d,id:%d,page:%d)\n", count, id, pageno);
        fprintf(file, "Page ref- (time:%d,id:%d,page:%d)\n", count, id, pageno);
        if (pageno < 0 || pageno >= process_pages[id]){
            printf("\tinvalid page(id:%d,page:%d)\n", id, pageno);
            fprintf(file, "\tinvalid page(id:%d,page:%d)\n", id, pageno);
            Per_pro_invalid[id]++;
            mmu_to_pro ms;
            ms.mtype = MMU_TO_PRO+id;
            ms.frame = INVALID_PAGE_REF;
            msgsnd(mq3, &ms, sizeof(ms), 0);
            terminated++;
            mmu_to_sch mshd;
            mshd.mtype = TERMINATED;
            msgsnd(mq2, &mshd, sizeof(mshd), 0);

        }
        else {
            global_table[(m*id)+pageno].last_access = count;
            if ( global_table[(m*id)+pageno].valid == 0){
                // page fault
                printf("\tpage fault (id:%d,page:%d)\n", id, pageno);
                fprintf(file, "\tpage fault (id:%d,page:%d)\n", id, pageno);
                Per_pro_pgf[id]++;
                int victim = find_victim_page(global_table, id, process_pages[id], m);
                int frame = find_free_frame(free_frame_list, f);
                // printf("hello1\n");
                if (frame == -1){
                    global_table[(m*id)+pageno].valid = 1;
                    global_table[(id*m)+pageno].last_access = count;
                    global_table[(id*m)+pageno].frame = global_table[(id*m)+victim].frame;
                    global_table[(id*m)+victim].valid = 0;
                    global_table[(id*m)+victim].last_access = count;
                    global_table[(id*m)+victim].frame = -1;
                    printf("\t\tvictim: %d\n", victim);
                }
                else {
                    // free frame available
                    global_table[(m*id)+pageno].valid = 1;
                    global_table[(id*m)+pageno].last_access = count;
                    global_table[(id*m)+pageno].frame = frame;
                    free_frame_list[frame]=0;
                    printf("\t\tfree\n");
                }

                free_frame_list[frame] = 0;         // not free frame 
                mmu_to_pro ms;
                ms.mtype = MMU_TO_PRO+id;
                ms.frame = PAGEFAULT;
                msgsnd(mq3, &ms, sizeof(ms), 0);

                mmu_to_sch mshd;
                mshd.mtype = PAGE_FAULT_HANDLED;
                msgsnd(mq2, &mshd, sizeof(mshd), 0);

            }
            else {
                printf("\tAvailabe page(id:%d,page:%d)\n", id, pageno);
                fprintf(file, "\tAvailabe page(id:%d,page:%d)\n", id, pageno);
                mmu_to_pro ms;
                ms.mtype = MMU_TO_PRO+id;
                ms.frame = global_table[(id*m)+pageno].frame;
                msgsnd(mq3, &ms, sizeof(ms), 0);
            }
            
            sleep(3);
        }
        if (terminated >= k){
            flag = 0;
        }
    }
    
    sigint_handle();
    printf("mmu terminating...\n");
    sleep(5);
    fflush(file);
    fclose(file);
    free(Per_pro_pgf);
    free(Per_pro_invalid);
    shmdt(global_table);
    shmdt(free_frame_list);
    shmdt(process_pages);
    return 0;
}


// Function to find the victim page number based on the LRU algorithm
int find_victim_page(const page_table_entry *page_table,int id,int process_range, int num_pages) {
    int victim_page = -1;
    int min_access_time = INT_MAX;

    for (int i = 0; i < process_range; i++) {
        if (page_table[i+(num_pages*id)].valid == 0) { // If page is empty, return it as victim
            continue;
        } else if (page_table[i+(num_pages*id)].valid == 1) { // If page is filled
            if (page_table[i+(num_pages*id)].last_access < min_access_time) {
                min_access_time = page_table[i+(num_pages*id)].last_access;
                victim_page = i;
            }
        }
    }

    return victim_page;
}

int find_free_frame(const int * list, int f){
    int ans = -1;
    for(int i=0; i<f; i++){
        if (list[i] == 1){
            ans = i;
            break;
        }
    }
    return ans;
}
void sigint_handle(){
    fflush(file);
    printf("PAGE FAULT PER PROCESS\n");
    printf("ID\t\tPGF\n");
    fprintf(file, "PAGE FAULT PER PROCESS\n");
    fprintf(file, "ID\t\tPGF\n");
    for(int i=0; i<k; i++){
        printf("%d\t\t%d\n", i, Per_pro_pgf[i]);
        fprintf(file, "%d\t\t%d\n", i, Per_pro_pgf[i]);
    }
    printf("INVALID REF PER PROCESS\n");
    fprintf(file, "INVALID REF PER PROCESS\n");
    printf("ID\t\tInvalid\n");
    fprintf(file, "ID\t\tInvalid\n");
    for(int i=0; i<k; i++){
        printf("%d\t\t%d\n", i, Per_pro_invalid[i]);
        fprintf(file, "%d\t\t%d\n", i, Per_pro_invalid[i]);
    }
    fflush(file);
    // fclose(file);
    // free(Per_pro_invalid);
    // free(Per_pro_pgf);
}