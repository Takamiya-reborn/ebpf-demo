#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>

int main(void)
{
    pid_t pid = getpid();
    printf("PID: %d\n", pid);
    fflush(stdout);

    const char *path = "/tmp/disk_delay_trigger.tmp";
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    char buf[8192];
    memset(buf, 'D', sizeof(buf));
    for (int i = 0; i < 16; i++) {
        ssize_t written = write(fd, buf, sizeof(buf));
        if (written < 0) {
            perror("write");
            close(fd);
            return 1;
        }
    }
    if (fsync(fd) < 0)
        perror("fsync");
    close(fd);
    return 0;
}
