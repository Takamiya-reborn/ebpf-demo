#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>
#include <sys/resource.h>

int main(void)
{
    pid_t pid = getpid();
    printf("PID: %d\n", pid);
    fflush(stdout);

    size_t size = 256 * 1024 * 1024;
    void *ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    memset(ptr, 0xab, 4096);
    if (mlock(ptr, 4096) == 0) {
        munlock(ptr, 4096);
    }

    size_t step = 4096;
    volatile char *p = ptr;
    for (size_t i = 0; i < size; i += step) {
        p[i] = (char)(i & 0xff);
        if ((i / step) % 1024 == 0)
            usleep(1000);
    }

    munmap(ptr, size);
    return 0;
}
