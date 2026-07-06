// SPDX-License-Identifier: GPL-2.0
// CPU Frequency Statistics - tracks CPU frequency scaling events
// Monitors DVFS (Dynamic Voltage and Frequency Scaling) state changes
#include "vmlinux.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"

// Manually define cpu_frequency tracepoint structure
// (trace_event_raw_cpu_frequency is not in this kernel's vmlinux.h)
struct cpu_frequency_args {
    struct trace_entry ent;
    u32 state;      // frequency in kHz
    u32 cpu_id;     // CPU ID
};

#define MAX_CPUS 256

struct freq_info {
    __u64 cur_freq;      // current/latest frequency (kHz)
    __u64 total_freq;    // cumulative frequency
    __u64 count;         // number of frequency changes
};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, MAX_CPUS);
    __type(key, __u32);           // CPU ID
    __type(value, struct freq_info);
} cpu_freq_map SEC(".maps");

// Alternative: also track via kprobe on arch_cpu_idle or cpufreq functions
// for cases where the power tracepoint is not available

SEC("tracepoint/power/cpu_frequency")
int handle_cpu_frequency(struct cpu_frequency_args *ctx)
{
    __u32 cpu = (__u32)(ctx->cpu_id);
    __u64 freq = (__u64)(ctx->state); // frequency in kHz

    if (cpu >= MAX_CPUS)
        return 0;

    struct freq_info *info = bpf_map_lookup_elem(&cpu_freq_map, &cpu);
    if (!info) {
        struct freq_info new_info = {};
        new_info.cur_freq = freq;
        new_info.total_freq = freq;
        new_info.count = 1;
        bpf_map_update_elem(&cpu_freq_map, &cpu, &new_info, BPF_ANY);
    } else {
        info->cur_freq = freq;
        __sync_fetch_and_add(&info->total_freq, freq);
        __sync_fetch_and_add(&info->count, 1);
    }

    return 0;
}

char LICENSE[] SEC("license") = "GPL";
