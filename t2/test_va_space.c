#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <errno.h>

/* Define the syscall number you registered in the kernel */
#define SYS_va_space_stat 463

/* The structure must exactly match your kernel implementation */
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

int main(int argc, char *argv[]) {
    pid_t target_pid = 0; /* Default to current process */
    struct addr_space_info info = {0};
    long res;

    /* Parse PID from command line if provided */
    if (argc > 1) {
        target_pid = atoi(argv[1]);
    }

    printf("Querying address space for PID: %d\n", target_pid == 0 ? getpid() : target_pid);

    /* Trigger system call 463 */
    res = syscall(SYS_va_space_stat, target_pid, &info);

    if (res < 0) {
	printf("\nERR: %d\n", res);
        perror("System call failed");
        return 1;
    }

    /* Print out the statistics */
    printf("\n--- Address Space Statistics ---\n");
    printf("Total VMAs:       %lu\n", info.num_vmas);
    printf("Anonymous VMAs:   %lu\n", info.num_anon);
    printf("File-backed VMAs: %lu\n", info.num_file);
    printf("W^X Violations:   %lu\n", info.num_w_and_x);
    printf("Total Mapped:     %lu bytes\n", info.total_mapped);
    printf("Total Resident:   %lu pages\n", info.total_resident);
    printf("Largest Gap:      %lu bytes\n", info.largest_gap);
    printf("Stack Size:       %lu bytes\n", info.stack_size);
    printf("Heap Size:        %lu bytes\n", info.heap_size);
    printf("--------------------------------\n");

    /* Quick sanity check based on assignment rules */
    if (info.num_anon + info.num_file != info.num_vmas) {
        printf("[!] Warning: Anonymous + File-backed VMAs do not equal Total VMAs!\n");
    }

    return 0;
}
