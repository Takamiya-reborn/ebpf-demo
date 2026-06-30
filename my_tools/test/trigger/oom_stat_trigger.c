#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>

int main(void)
{
    pid_t pid = getpid();
    printf("PID: %d\n", pid);
    fflush(stdout);

    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        return 1;
    }

    if (child == 0) {
        size_t alloc_size = 256ULL * 1024 * 1024;
        while (1) {
            void *p = malloc(alloc_size);
            if (!p) {
                sleep(60);
                break;
            }
            memset(p, 0, alloc_size);
            sleep(1);
        }
        return 0;
    }

    int status;
    waitpid(child, &status, 0);
    printf("child exit status=%d\n", status);
    return 0;
}
