#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <sys/socket.h>
#include <libbpf.h>
#include "bpf.h"
#include "socket_stat.skel.h"

struct family_info {
    uint64_t count;
};

static volatile bool exiting = false;

static void sig_handler(int sig)
{
    exiting = true;
}

static const char *family_name(uint32_t family)
{
    switch (family) {
    case AF_INET:
        return "AF_INET";
    case AF_INET6:
        return "AF_INET6";
    case AF_UNIX:
        return "AF_UNIX";
    case AF_PACKET:
        return "AF_PACKET";
    default:
        return "OTHER";
    }
}

int main(int argc, char **argv)
{
    struct socket_stat_bpf_linked *skel;
    int err;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    skel = socket_stat_bpf_linked__open_and_load();
    if (!skel) {
        fprintf(stderr, "Failed to load BPF skeleton\n");
        return 1;
    }

    err = socket_stat_bpf_linked__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF program: %d\n", err);
        goto cleanup;
    }

    printf("Monitoring socket creation by family, press Ctrl+C to exit.\n");
    printf("%-10s %-10s\n", "FAMILY", "COUNT");
    printf("-------------------------\n");

    while (!exiting) {
        sleep(2);
        uint32_t key = 0, next_key;
        struct family_info info;

        while (bpf_map__get_next_key(skel->maps.family_count, &key, &next_key, sizeof(key)) == 0) {
            key = next_key;
            if (bpf_map__lookup_elem(skel->maps.family_count, &key, sizeof(key), &info, sizeof(info), 0) == 0) {
                printf("%-10s %-10llu\n", family_name(key), (unsigned long long)info.count);
            }
        }
        printf("-------------------------\n");
    }

cleanup:
    socket_stat_bpf_linked__destroy(skel);
    return err < 0 ? -err : err;
}
