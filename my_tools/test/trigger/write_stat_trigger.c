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

    const char *path = "/tmp/write_stat_trigger.tmp";
    int fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    const char *content = "write stat trigger\n";
    ssize_t written = write(fd, content, strlen(content));
    if (written < 0) {
        perror("write");
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}
