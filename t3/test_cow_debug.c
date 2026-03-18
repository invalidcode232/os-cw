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
#define TEST_PAGES 10

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
    printf("      ULTIMATE COW DIAGNOSTIC TEST SCRIPT         \n");
    printf("==================================================\n\n");

    char *shared_mem = mmap(NULL, TEST_PAGES * PAGE_SIZE, PROT_READ | PROT_WRITE, 
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    memset(shared_mem, 'A', TEST_PAGES * PAGE_SIZE);

    syscall(SYS_cow_info, 0, &info1);
    printf("[PARENT BEFORE FORK] Fault count: %lu | Total COW: %lu\n", 
           info1.cow_fault_count, info1.total_cow);

    pid_t pid = fork();

    if (pid == 0) {
        /* ================= CHILD PROCESS ================= */
        struct cow_info child_start = {0}, child_end = {0};
        int child_passed = 1;

        syscall(SYS_cow_info, 0, &child_start);
        printf("\n---> [CHILD WAKE UP] Fault count: %lu (Should be < 20)\n", child_start.cow_fault_count);
        printf("---> [CHILD WAKE UP] Total COW: %lu (Should be >= %d)\n", child_start.total_cow, TEST_PAGES);

        if (child_start.cow_fault_count > 20) {
            printf("\n🚨 ERROR: Fault count is huge! Your fork.c patch didn't work or you aren't booted into the new kernel!\n");
            child_passed = 0;
        }

        /* Trigger COW faults */
        int pages_to_write = 5;
        for (int i = 0; i < pages_to_write; i++) {
            shared_mem[i * PAGE_SIZE] = 'B';
        }

        syscall(SYS_cow_info, 0, &child_end);
        printf("\n---> [CHILD AFTER WRITES] Fault count: %lu (Expected ~%lu)\n", 
               child_end.cow_fault_count, child_start.cow_fault_count + pages_to_write);
        printf("---> [CHILD AFTER WRITES] Total COW: %lu (Expected <= %lu)\n", 
               child_end.total_cow, child_start.total_cow - pages_to_write);

        if (child_end.cow_fault_count < (child_start.cow_fault_count + pages_to_write)) {
            printf("🚨 ERROR: Fault count didn't increase enough! memory.c patch failed.\n");
            child_passed = 0;
        }

        if (child_end.total_cow > (child_start.total_cow - pages_to_write)) {
            printf("🚨 ERROR: Total COW pages didn't drop! Page table scanner issue.\n");
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
            printf("✅ CHILD PASSED ALL CHECKS!\n");
        } else {
            printf("❌ CHILD FAILED! (See reasons above)\n");
        }

        syscall(SYS_cow_info, 0, &info2);
        printf("[PARENT AFTER WAIT] Fault count: %lu (Should be close to %lu)\n", 
               info2.cow_fault_count, info1.cow_fault_count);
               
        if ((info2.cow_fault_count - info1.cow_fault_count) >= 20) {
            printf("🚨 ERROR: Parent fault count spiked wildly! It is improperly sharing the child's counter.\n");
        }
    }

    munmap(shared_mem, TEST_PAGES * PAGE_SIZE);
    return 0;
}
