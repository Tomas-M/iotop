#include "iotop.h"

#include <sys/syscall.h>
#include <stdio.h>
#include <unistd.h>
#include <sched.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <linux/sched.h>

enum {
    IOPRIO_CLASS_NONE,
    IOPRIO_CLASS_RT,
    IOPRIO_CLASS_BE,
    IOPRIO_CLASS_IDLE,
    IOPRIO_CLASS_MAX
};

enum {
    IOPRIO_WHO_PROCESS = 1,
    IOPRIO_WHO_PGRP,
    IOPRIO_WHO_USER
};

#define IOPRIO_CLASS_SHIFT 13
#define IOPRIO_STR_MAXSIZ  7
#define IOPRIO_STR_FORMAT "%2s/%1i"

const char *str_ioprio_class[] = { "-", "rt", "be", "id" };

inline int get_ioprio_from_sched(pid_t pid)
{
    int scheduler = sched_getscheduler(pid);
    int nice = getpriority(PRIO_PROCESS, pid);
    int ioprio_nice = (nice + 20) / 5;

    if (scheduler == SCHED_FIFO || scheduler == SCHED_RR)
        return (IOPRIO_CLASS_RT << IOPRIO_CLASS_SHIFT) + ioprio_nice;
    if (scheduler == SCHED_IDLE)
        return IOPRIO_CLASS_IDLE << IOPRIO_CLASS_SHIFT;
    return (IOPRIO_CLASS_BE << IOPRIO_CLASS_SHIFT) + ioprio_nice;
}

inline int get_ioprio(pid_t pid)
{
    int io_prio, io_class;

    io_prio = syscall(SYS_ioprio_get, IOPRIO_WHO_PROCESS, pid);
    io_class = io_prio >> IOPRIO_CLASS_SHIFT;
    if (!io_class)
        return get_ioprio_from_sched(pid);
    return io_prio;
}

const char *str_ioprio(int io_prio)
{
    const static char corrupted[] = "xx/x";
    static char buf[IOPRIO_STR_MAXSIZ];
    int io_class = io_prio >> IOPRIO_CLASS_SHIFT;

    io_prio &= 0xff;

    if (io_class >= IOPRIO_CLASS_MAX)
        return corrupted;

    snprintf(
        buf,
        IOPRIO_STR_MAXSIZ,
        IOPRIO_STR_FORMAT,
        str_ioprio_class[io_class],
        io_prio
    );

    return (const char *) buf;
}

int ioprio_value(const char *prio, int data)
{
    int i;

    for (i=0; i < sizeof(str_ioprio_class) / sizeof(*str_ioprio_class); i++)
        if (!strcmp(prio, str_ioprio_class[i]))
            return (i << IOPRIO_CLASS_SHIFT) | data;

    return (0 << IOPRIO_CLASS_SHIFT) | data;
}

int set_ioprio(int which, int who, const char *ioprio_class, int ioprio_data)
{
    int ioprio_val = ioprio_value(ioprio_class, ioprio_data);

    return syscall(SYS_ioprio_set, which, who, ioprio_val);
}

