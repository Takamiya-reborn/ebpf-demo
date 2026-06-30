#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>

int main(void)
{
    pid_t pid = getpid();
    printf("PID: %d\n", pid);
    fflush(stdout);

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        perror("socketpair");
        return 1;
    }

    const char *msg = "socket trigger";
    if (write(sv[0], msg, strlen(msg)) < 0) {
        perror("write");
    }

    char buf[64];
    read(sv[1], buf, sizeof(buf));
    close(sv[0]);
    close(sv[1]);
    return 0;
}
