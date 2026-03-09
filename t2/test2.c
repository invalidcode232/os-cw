#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>

#define SYS_va_space_stat 463

struct addr_space_info {
    unsigned long num_vmas;
    unsigned long num_anon;
    unsigned long num_file;
    unsigned long num_w_and_x;
    unsigned long total_mapped;
    unsigned long total_resident;
    unsigned long largest_gap;
    unsigned long stack_size;
    unsigned long heap_size;
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
    struct addr_space_info info1 = {0};
    struct addr_space_info info2 = {0};
    long res;
    void *wx_mem;

    printf("==================================================\n");
    printf("   STARTING SYS_VA_SPACE_STAT AUTOMATED TESTS     \n");
    printf("==================================================\n\n");

    /* ---------------------------------------------------------
       1. NORMAL CASES
       --------------------------------------------------------- */
    printf("--- Normal Execution Tests ---\n");
    
    res = syscall(SYS_va_space_stat, 0, &info1);
    EXPECT_TRUE("PID 0 (Self) returns success (0)", res == 0);
    
    res = syscall(SYS_va_space_stat, getpid(), &info2);
    EXPECT_TRUE("Explicit PID (getpid()) returns success (0)", res == 0);
    
    EXPECT_TRUE("PID 0 and getpid() yield same VMA count", info1.num_vmas == info2.num_vmas);


    /* ---------------------------------------------------------
       2. EDGE CASES & ERROR HANDLING
       --------------------------------------------------------- */
    printf("\n--- Edge Cases & Error Handling ---\n");
    
    res = syscall(SYS_va_space_stat, -1, &info1);
    EXPECT_TRUE("Negative PID returns -1", res == -1);
    EXPECT_TRUE("Negative PID sets errno to EINVAL", errno == EINVAL);
    
    res = syscall(SYS_va_space_stat, 9999999, &info1); /* Assuming this PID doesn't exist */
    EXPECT_TRUE("Non-existent PID returns -1", res == -1);
    EXPECT_TRUE("Non-existent PID sets errno to ESRCH", errno == ESRCH);

    res = syscall(SYS_va_space_stat, 0, NULL);
    EXPECT_TRUE("NULL pointer for struct returns -1", res == -1);
    EXPECT_TRUE("NULL pointer sets errno to EINVAL/EFAULT", errno == EINVAL || errno == EFAULT);


    /* ---------------------------------------------------------
       3. DATA VALIDITY & MATH CHECKS
       --------------------------------------------------------- */
    printf("\n--- Data Validity Tests ---\n");
    
    syscall(SYS_va_space_stat, 0, &info1); /* Get fresh baseline */
    
    EXPECT_TRUE("Math: Anon VMAs + File VMAs == Total VMAs", 
                (info1.num_anon + info1.num_file) == info1.num_vmas);
    
    EXPECT_TRUE("Total mapped bytes > 0", info1.total_mapped > 0);
    EXPECT_TRUE("Total resident pages > 0", info1.total_resident > 0);
    EXPECT_TRUE("Stack size is > 0", info1.stack_size > 0);
    
    /* Force some heap allocation to ensure heap > 0 */
    void *heap_ptr = malloc(1024);
    syscall(SYS_va_space_stat, 0, &info2);
    EXPECT_TRUE("Heap size is > 0 after malloc", info2.heap_size > 0);
    free(heap_ptr);


    /* ---------------------------------------------------------
       4. SECURITY LOGIC (W^X VIOLATION DETECTION)
       --------------------------------------------------------- */
    printf("\n--- Security Logic (W^X) Tests ---\n");
    
    unsigned long initial_wx = info1.num_w_and_x;
    
    /* Deliberately map a page with Write AND Execute permissions */
    wx_mem = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, 
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                  
    if (wx_mem == MAP_FAILED) {
        printf("[ SKIP ] Could not allocate W^X memory to test.\n");
    } else {
        syscall(SYS_va_space_stat, 0, &info2);
        EXPECT_TRUE("W^X counter increases when PROT_WRITE|PROT_EXEC memory is mapped", 
                    info2.num_w_and_x == (initial_wx + 1));
        
        /* Clean up */
        munmap(wx_mem, 4096);
    }

    /* ---------------------------------------------------------
       5. KERNEL THREAD TEST (PID 2 is usually kthreadd)
       --------------------------------------------------------- */
    printf("\n--- Kernel Thread Test ---\n");
    res = syscall(SYS_va_space_stat, 2, &info1);
    printf("\nres (exp: -1): %d\n", res);
    printf("\nres (exp: 22): %d\n", errno);
    /* Kernel threads have no mm_struct, so our syscall should catch it and return -EINVAL */
    EXPECT_TRUE("Kernel thread (PID 2) returns -1", res == -1);
    EXPECT_TRUE("Kernel thread sets errno to EINVAL", errno == EINVAL);


    /* ---------------------------------------------------------
       SUMMARY
       --------------------------------------------------------- */
    printf("\n==================================================\n");
    printf("TEST RESULTS: %d / %d PASSED\n", tests_passed, tests_run);
    printf("==================================================\n");
    
    if (tests_passed == tests_run) {
        printf("SUCCESS: Your system call implementation looks perfect!\n");
        return 0;
    } else {
        printf("WARNING: Some tests failed. Check your kernel code.\n");
        return 1;
    }
}
