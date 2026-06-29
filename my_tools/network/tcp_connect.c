#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <libbpf.h>
#include "bpf.h"
#include "tcp_connect.skel.h"

struct conn_info {
    __u64 count;
    char comm[16];
};

static volatile bool exiting = false;

static void sig_handler(int sig)
{
    exiting = true;
}

int main(int argc, char **argv)
{
    struct tcp_connect_bpf_linked *skel;
    int err;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    skel = tcp_connect_bpf_linked__open_and_load();
    if (!skel) {
        fprintf(stderr, "Failed to load BPF skeleton\n");
        return 1;
    }

    err = tcp_connect_bpf_linked__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF program: %d\n", err);
        goto cleanup;
    }

    fprintf(stderr, "Monitoring TCP connect calls, press Ctrl+C to exit.\n");

    while (!exiting) {
        sleep(2);
        __u32 pid = 0, next_pid;
        struct conn_info info;

        while (bpf_map__get_next_key(skel->maps.conn_count, &pid, &next_pid, sizeof(pid)) == 0) {
            pid = next_pid;
            if (bpf_map__lookup_elem(skel->maps.conn_count, &pid, sizeof(pid), &info, sizeof(info), 0) == 0) {
                printf("tcp_connect_pid_%u_comm_%s: %llu\n", pid, info.comm, info.count);
            }
        }
    }

cleanup:
    tcp_connect_bpf_linked__destroy(skel);
    return err < 0 ? -err : err;
}
