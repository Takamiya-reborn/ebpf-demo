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

    const char *path = "/tmp/mmap_stat_trigger.tmp";
    int fd = open(path, O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    if (ftruncate(fd, 4096) < 0) {
        perror("ftruncate");
        close(fd);
        return 1;
    }

    void *addr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }

    memset(addr, 'M', 4096);
    msync(addr, 4096, MS_SYNC);
    munmap(addr, 4096);
    close(fd);
    return 0;
}
