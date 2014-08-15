#ifndef __IOTOP_H__
#define __IOTOP_H__

#include <unistd.h>
#include <asm/types.h>

#define VERSION "0.1"

extern const char *str_ioprio_class[];

enum {
    IOPRIO_CLASS_NONE,
    IOPRIO_CLASS_RT,
    IOPRIO_CLASS_BE,
    IOPRIO_CLASS_IDLE
};

enum {
    IOPRIO_WHO_PROCESS = 1,
    IOPRIO_WHO_PGRP,
    IOPRIO_WHO_USER
};

struct xxxid_stats {
    pid_t tid;
    __u64 cpu_run_real_total;
    __u64 swapin_delay_total;
    __u64 blkio_delay_total;
    __u64 read_bytes;
    __u64 write_bytes;
    int ioprio;
    int ioprio_class;

    int euid;
    char *cmdline;

    void *__next;
};

void nl_init(void);
void nl_term(void);

int nl_xxxid_info(pid_t xxxid, int isp, struct xxxid_stats *stats);
void dump_xxxid_stats(struct xxxid_stats *stats);

struct xxxid_stats* fetch_data(int processes, int (*filter)(struct xxxid_stats *));
void free_stats_chain(struct xxxid_stats *chain);

#endif // __IOTOP_H__

