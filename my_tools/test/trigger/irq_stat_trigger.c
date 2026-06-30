#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

int main(void)
{
    pid_t pid = getpid();
    printf("PID: %d\n", pid);
    fflush(stdout);

    const char *path = "/tmp/irq_stat_trigger.tmp";
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd >= 0) {
        char buf[4096];
        memset(buf, 'I', sizeof(buf));
        for (int i = 0; i < 32; i++) {
            if (write(fd, buf, sizeof(buf)) < 0)
                break;
        }
        fsync(fd);
        close(fd);
    }

    for (int i = 0; i < 200; i++) {
        char buf[256];
        snprintf(buf, sizeof(buf), "irq-%d\n", i);
        write(STDOUT_FILENO, buf, strlen(buf));
        usleep(5000);
    }

    return 0;
}
