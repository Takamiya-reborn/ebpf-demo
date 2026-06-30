#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>

int main(void)
{
    pid_t pid = getpid();
    printf("PID: %d\n", pid);
    fflush(stdout);

    size_t size = 8 * 1024 * 1024;
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    volatile char *p = ptr;
    for (size_t i = 0; i < size; i += 4096) {
        p[i] = (char)(i & 0xff);
    }

    munmap(ptr, size);
    return 0;
}
