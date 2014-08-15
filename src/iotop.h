#ifndef __IOTOP_H__
#define __IOTOP_H__

#include <unistd.h>
#include <asm/types.h>

#define VERSION "0.1"

// const char *str_ioprio_class[] = { "-", "rt", "be", "id" };
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
    __u64 cpu_run_real_total;
    __u64 swapin_delay_total;
    __u64 blkio_delay_total;
    __u64 read_bytes;
    __u64 write_bytes;
    int ioprio;
    int ioprio_class;
};

void nl_init(void);
void nl_term(void);

int nl_xxxid_info(pid_t xxxid, int isp, struct xxxid_stats *stats);
void dump_xxxid_stats(struct xxxid_stats *stats);

#endif // __IOTOP_H__

