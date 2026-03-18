#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

#define SYS_cow_info 464
#define PAGE_SIZE 4096
#define TEST_PAGES 10000     /* 40 MB of memory to guarantee a massive signal */
#define PAGES_TO_WRITE 8000  /* We will force 8,000 COW faults */

struct cow_info {
    unsigned long total_cow;
    unsigned long anon_cow;
    unsigned long file_cow;
    unsigned long total_writable;
    unsigned long num_cow_vmas;
    unsigned long cow_fault_count;
};

int main() {
    struct cow_info info1 = {0};
    struct cow_info info2 = {0};
    
    printf("\n==================================================\n");
    printf("   ULTIMATE COW DIAGNOSTIC: THE MASSIVE SIGNAL    \n");
    printf("==================================================\n\n");

    /* Map 10,000 pages */
    char *shared_mem = mmap(NULL, TEST_PAGES * PAGE_SIZE, PROT_READ | PROT_WRITE, 
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (shared_mem == MAP_FAILED) {
        perror("mmap failed");
        return 1;
    }

    /* Force physical allocation in the parent */
    memset(shared_mem, 'A', TEST_PAGES * PAGE_SIZE);

    syscall(SYS_cow_info, 0, &info1);
    printf("[PARENT BEFORE FORK] Fault count: %lu | Total COW pages: %lu\n", 
           info1.cow_fault_count, info1.total_cow);

    pid_t pid = fork();

    if (pid == 0) {
        /* ================= CHILD PROCESS ================= */
        struct cow_info child_start = {0}, child_end = {0};
        int child_passed = 1;

        syscall(SYS_cow_info, 0, &child_start);
        printf("\n---> [CHILD WAKE UP] Fault count: %lu (Should be < 100 noise)\n", child_start.cow_fault_count);
        printf("---> [CHILD WAKE UP] Total COW pages: %lu (Should be >= %d)\n", child_start.total_cow, TEST_PAGES);

        if (child_start.cow_fault_count > 1000) {
            printf("\n🚨 ERROR: Fault count is HUGE right after fork. You inherited the parent's counter!\n");
            printf("FIX: Your fork.c patch is not active in this kernel.\n");
            child_passed = 0;
        }

        printf("\n---> [CHILD] Writing to %d pages to trigger MASSIVE COW faults...\n", PAGES_TO_WRITE);
        /* Trigger an undeniable number of COW faults */
        for (int i = 0; i < PAGES_TO_WRITE; i++) {
            shared_mem[i * PAGE_SIZE] = 'B';
        }

        syscall(SYS_cow_info, 0, &child_end);
        
        unsigned long faults_generated = child_end.cow_fault_count - child_start.cow_fault_count;
        unsigned long cow_pages_lost = child_start.total_cow - child_end.total_cow;

        printf("---> [CHILD AFTER WRITES] Faults generated: %lu (Expected ~%d)\n", faults_generated, PAGES_TO_WRITE);
        printf("---> [CHILD AFTER WRITES] COW pages lost: %lu (Expected ~%d)\n", cow_pages_lost, PAGES_TO_WRITE);

        /* We check if it's within a reasonable window of our massive signal */
        if (faults_generated < PAGES_TO_WRITE || faults_generated > (PAGES_TO_WRITE + 500)) {
            printf("🚨 ERROR: Fault count did not increase by the expected massive amount! memory.c patch failed.\n");
            child_passed = 0;
        }

        if (cow_pages_lost < PAGES_TO_WRITE || cow_pages_lost > (PAGES_TO_WRITE + 500)) {
            printf("🚨 ERROR: Total COW pages didn't drop by the expected amount! Page table scanner issue.\n");
            child_passed = 0;
        }

        if (child_passed) exit(0);
        else exit(1);
    } 
    else {
        /* ================= PARENT PROCESS ================= */
        int status;
        waitpid(pid, &status, 0); 
        
        printf("\n==================================================\n");
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf("✅ CHILD PASSED ALL CHECKS! YOUR SYSCALL IS FLAWLESS!\n");
        } else {
            printf("❌ CHILD FAILED! (Read the raw numbers above to see why)\n");
        }

        syscall(SYS_cow_info, 0, &info2);
        unsigned long parent_faults_generated = info2.cow_fault_count - info1.cow_fault_count;
        printf("[PARENT AFTER WAIT] Faults generated: %lu (Should be < 100 noise)\n", parent_faults_generated);
               
        if (parent_faults_generated > 1000) {
            printf("🚨 ERROR: Parent fault count spiked wildly! It is improperly sharing the child's counter.\n");
        }
    }

    munmap(shared_mem, TEST_PAGES * PAGE_SIZE);
    return 0;
}
