#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

#define SYS_cow_info 463
#define PAGE_SIZE 4096
#define TEST_PAGES 10

/* The exact structure specified in the assignment */
struct cow_info {
    unsigned long total_cow;
    unsigned long anon_cow;
    unsigned long file_cow;
    unsigned long total_writable;
    unsigned long num_cow_vmas;
    unsigned long cow_fault_count;
};

/* Global test counters */
int tests_run = 0;
int tests_passed = 0;

/* Helper macro for clean test output */
#define EXPECT_TRUE(name, condition) do { \
    tests_run++; \
    if (condition) { \
        printf("[ PASS ] %s\n", name); \
        tests_passed++; \
    } else { \
        printf("[ FAIL ] %s\n", name); \
    } \
} while(0)

int main() {
    struct cow_info info1 = {0};
    struct cow_info info2 = {0};
    long res;

    printf("==================================================\n");
    printf("     STARTING SYS_COW_INFO AUTOMATED TESTS        \n");
    printf("==================================================\n\n");

    /* ---------------------------------------------------------
       1. EDGE CASES & ERROR HANDLING
       --------------------------------------------------------- */
    printf("--- Edge Cases & Error Handling ---\n");
    
    res = syscall(SYS_cow_info, -1, &info1);
    EXPECT_TRUE("Negative PID returns -1", res == -1);
    EXPECT_TRUE("Negative PID sets errno to EINVAL", errno == EINVAL);
    
    res = syscall(SYS_cow_info, 9999999, &info1); /* Assuming PID doesn't exist */
    EXPECT_TRUE("Non-existent PID returns -1", res == -1);
    EXPECT_TRUE("Non-existent PID sets errno to ESRCH", errno == ESRCH);

    res = syscall(SYS_cow_info, 0, NULL);
    EXPECT_TRUE("NULL pointer for struct returns -1", res == -1);
    EXPECT_TRUE("NULL pointer sets errno to EINVAL/EFAULT", errno == EINVAL || errno == EFAULT);

    /* Test kthreadd (PID 2) which has no mm_struct */
    res = syscall(SYS_cow_info, 2, &info1);
    EXPECT_TRUE("Kernel thread (PID 2) returns -1", res == -1);
    EXPECT_TRUE("Kernel thread sets errno to EINVAL", errno == EINVAL);


    /* ---------------------------------------------------------
       2. DATA VALIDITY & MATH CHECKS
       --------------------------------------------------------- */
    printf("\n--- Data Validity Tests ---\n");
    
    res = syscall(SYS_cow_info, 0, &info1);
    EXPECT_TRUE("PID 0 (Self) returns success (0)", res == 0);
    
    EXPECT_TRUE("Math: Anon COW + File COW == Total COW", 
                (info1.anon_cow + info1.file_cow) == info1.total_cow);
    
    EXPECT_TRUE("Total writable pages > 0", info1.total_writable > 0);


    /* ---------------------------------------------------------
       3. LIVE COPY-ON-WRITE & FAULT TRACKING TEST
       --------------------------------------------------------- */
    printf("\n--- Live Fork() & COW Fault Tests ---\n");

    /* Allocate 10 pages of anonymous memory */
    char *shared_mem = mmap(NULL, TEST_PAGES * PAGE_SIZE, PROT_READ | PROT_WRITE, 
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (shared_mem == MAP_FAILED) {
        perror("mmap failed");
        return 1;
    }

    /* Write to the memory so it actually gets mapped to physical pages 
       (avoids the kernel's read-only zero-page optimization) */
    memset(shared_mem, 'A', TEST_PAGES * PAGE_SIZE);

    /* Get parent's baseline before fork */
    syscall(SYS_cow_info, 0, &info1);

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork failed");
        return 1;
    } 
    else if (pid == 0) {
        /* ================= CHILD PROCESS ================= */
        struct cow_info child_start = {0};
        struct cow_info child_end = {0};
        int child_passed = 0;
        int child_total = 0;

        #define CHILD_EXPECT(name, cond) do { \
            child_total++; \
            if (cond) { \
                printf("[ PASS ] (Child) %s\n", name); \
                child_passed++; \
            } else { \
                printf("[ FAIL ] (Child) %s\n", name); \
            } \
        } while(0)

        /* 1. Check child initialization */
        syscall(SYS_cow_info, 0, &child_start);
        CHILD_EXPECT("Child COW fault count initializes to 0", child_start.cow_fault_count == 0);
        CHILD_EXPECT("Child sees COW pages due to fork", child_start.total_cow >= TEST_PAGES);

        /* 2. Intentionally trigger COW faults by writing to 5 of the 10 shared pages */
        int pages_to_write = 5;
        for (int i = 0; i < pages_to_write; i++) {
            shared_mem[i * PAGE_SIZE] = 'B';
        }

        /* 3. Verify counters updated correctly (Tolerating background libc faults) */
        syscall(SYS_cow_info, 0, &child_end);
        
        CHILD_EXPECT("Child COW fault count increments by AT LEAST 5", 
                     child_end.cow_fault_count >= pages_to_write);
                     
        CHILD_EXPECT("Child total COW count decreases by AT LEAST 5", 
                     child_end.total_cow <= (child_start.total_cow - pages_to_write));

        /* Pass success/fail state back to parent via exit code */
        if (child_passed == child_total) exit(0);
        else exit(1);
    } 
    else {
        /* ================= PARENT PROCESS ================= */
        int status;
        waitpid(pid, &status, 0); /* Wait for child to finish testing */

        tests_run++;
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            printf("[ PASS ] Child process COW assertions succeeded\n");
            tests_passed++;
        } else {
            printf("[ FAIL ] Child process COW assertions failed\n");
        }

        /* Ensure parent's fault count didn't artificially inflate massively from child */
        syscall(SYS_cow_info, 0, &info2);
        
        int pages_to_write = 5;
        /* The parent might take 1 or 2 faults from waitpid() or libc, but if it spikes 
           by 5+, it means it is illegally sharing the child's counter */
        EXPECT_TRUE("Parent COW fault count isolated from child", 
                    (info2.cow_fault_count - info1.cow_fault_count) < pages_to_write);
    }

    munmap(shared_mem, TEST_PAGES * PAGE_SIZE);

    /* ---------------------------------------------------------
       SUMMARY
       --------------------------------------------------------- */
    printf("\n==================================================\n");
    printf("TEST RESULTS: %d / %d PASSED\n", tests_passed, tests_run);
    printf("==================================================\n");
    
    if (tests_passed == tests_run) {
        printf("SUCCESS: Your COW tracking implementation is flawless!\n");
        return 0;
    } else {
        printf("WARNING: Some tests failed. Check your kernel code.\n");
        return 1;
    }
}
