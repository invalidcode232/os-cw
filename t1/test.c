#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/syscall.h>

struct mem_ops {
    long mmap_count, mmap_bytes;
    long munmap_count, munmap_bytes;
    long mprotect_count, mprotect_bytes;
    long brk_count, brk_bytes;
};

// Reads the counters for the current process
int get_mem_ops(struct mem_ops *ops) {
    char buf[512];
    int fd = open("/proc/self/mem_ops", O_RDONLY);
    if (fd < 0) {
        perror("Failed to open /proc/self/mem_ops. Did you compile and load the kernel?");
        exit(1);
    }
    
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    buf[n] = '\0';
    close(fd);

    int parsed = sscanf(buf, 
           "mmap %ld %ld\n"
           "munmap %ld %ld\n"
           "mprotect %ld %ld\n"
           "brk %ld %ld\n",
           &ops->mmap_count, &ops->mmap_bytes,
           &ops->munmap_count, &ops->munmap_bytes,
           &ops->mprotect_count, &ops->mprotect_bytes,
           &ops->brk_count, &ops->brk_bytes);

    printf("\n%s\n", buf);
    if (parsed != 8) {
        printf("[FAIL] Format of /proc/self/mem_ops is incorrect.\n");
        printf("Expected 4 lines with 'name count bytes'. Got:\n%s\n", buf);
        exit(1);
    }
    return 0;
}

void print_result(const char *test_name, int passed) {
    if (passed)
        printf("[PASS] %s\n", test_name);
    else
        printf("[FAIL] %s\n", test_name);
}

int main() {
    struct mem_ops before, after;
    void *ptr;
    long diff_count, diff_bytes;

    printf("--- Starting Kernel Mod Tests ---\n\n");

    // Warm up the PLT to prevent glibc from doing lazy binding mmaps during our tests
    get_mem_ops(&before);

    // ---------------------------------------------------------
    // TEST 1: Successful mmap
    // ---------------------------------------------------------
    get_mem_ops(&before);
    ptr = mmap(NULL, 8192, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    get_mem_ops(&after);
    
    diff_count = after.mmap_count - before.mmap_count;
    diff_bytes = after.mmap_bytes - before.mmap_bytes;
    print_result("mmap (Success)", diff_count == 1 && diff_bytes == 8192);

    // ---------------------------------------------------------
    // TEST 2: Failed mmap (Should NOT increment)
    // ---------------------------------------------------------
    get_mem_ops(&before);
    // Force a failure by providing an invalid file descriptor
    mmap(NULL, 4096, PROT_READ, MAP_SHARED, -99, 0); 
    get_mem_ops(&after);
    
    diff_count = after.mmap_count - before.mmap_count;
    diff_bytes = after.mmap_bytes - before.mmap_bytes;
    print_result("mmap (Failure ignored)", diff_count == 0 && diff_bytes == 0);

    // ---------------------------------------------------------
    // TEST 3: Successful munmap
    // ---------------------------------------------------------
    get_mem_ops(&before);
    munmap(ptr, 8192);
    get_mem_ops(&after);
    
    diff_count = after.munmap_count - before.munmap_count;
    diff_bytes = after.munmap_bytes - before.munmap_bytes;
    print_result("munmap (Success)", diff_count == 1 && diff_bytes == 8192);

    // ---------------------------------------------------------
    // TEST 4: Successful mprotect
    // ---------------------------------------------------------
    ptr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    get_mem_ops(&before);
    mprotect(ptr, 4096, PROT_READ);
    get_mem_ops(&after);
    
    diff_count = after.mprotect_count - before.mprotect_count;
    diff_bytes = after.mprotect_bytes - before.mprotect_bytes;
    print_result("mprotect (Success)", diff_count == 1 && diff_bytes == 4096);

    // ---------------------------------------------------------
    // TEST 5: Failed mprotect (Should NOT increment)
    // ---------------------------------------------------------
    get_mem_ops(&before);
    mprotect(NULL, 4096, PROT_READ); // Fails due to NULL pointer
    get_mem_ops(&after);
    
    diff_count = after.mprotect_count - before.mprotect_count;
    diff_bytes = after.mprotect_bytes - before.mprotect_bytes;
    print_result("mprotect (Failure ignored)", diff_count == 0 && diff_bytes == 0);

    // ---------------------------------------------------------
    // TEST 6: brk (Growth and Shrinkage)
    // ---------------------------------------------------------
    get_mem_ops(&before);
    
    // Use raw syscall to bypass glibc malloc/sbrk interference
    unsigned long current_brk = syscall(SYS_brk, 0);
    syscall(SYS_brk, current_brk + 4096); // Grow by 4096
    syscall(SYS_brk, current_brk);        // Shrink by 4096
    
    get_mem_ops(&after);
    
    diff_count = after.brk_count - before.brk_count;
    diff_bytes = after.brk_bytes - before.brk_bytes;
    
    // Total absolute change: 4096 (growth) + 4096 (shrinkage) = 8192
    print_result("brk (Growth + Shrinkage)", diff_count == 2 && diff_bytes == 8192);

    // ---------------------------------------------------------
    // TEST 7: Fork Inheritance (Counters must be 0)
    // ---------------------------------------------------------
    fflush(stdout); // Flush before fork
    pid_t pid = fork();
    
    if (pid == 0) {
        // Child process
        struct mem_ops child_ops;
        get_mem_ops(&child_ops);
        
        int all_zero = (child_ops.mmap_count == 0 && child_ops.mmap_bytes == 0 &&
                        child_ops.munmap_count == 0 && child_ops.munmap_bytes == 0 &&
                        child_ops.mprotect_count == 0 && child_ops.mprotect_bytes == 0 &&
                        child_ops.brk_count == 0 && child_ops.brk_bytes == 0);
        
        print_result("fork (Child counters start at 0)", all_zero);
        
        if (!all_zero) {
            printf("Child inherited non-zero counters:\n");
            printf("mmap %ld %ld\nmunmap %ld %ld\nmprotect %ld %ld\nbrk %ld %ld\n",
                   child_ops.mmap_count, child_ops.mmap_bytes,
                   child_ops.munmap_count, child_ops.munmap_bytes,
                   child_ops.mprotect_count, child_ops.mprotect_bytes,
                   child_ops.brk_count, child_ops.brk_bytes);
        }
        exit(0);
    } else {
        // Parent waits for child
        wait(NULL);
    }

    printf("\n--- Tests Complete ---\n");
    return 0;
}
