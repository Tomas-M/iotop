#include "iotop.h"

#include <sched.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/resource.h>

#define IOPRIO_CLASS_SHIFT 13
#define IOPRIO_STR_MAXSIZ  10
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

inline int ioprio2class(int ioprio)
{
    return ioprio >> IOPRIO_CLASS_SHIFT;
}

inline int ioprio2prio(int ioprio)
{
    return ioprio & ((1 << IOPRIO_CLASS_SHIFT) - 1);
}

inline const char *str_ioprio(int io_prio)
{
    static const char corrupted[] = "xx/x";
    static char buf[IOPRIO_STR_MAXSIZ];
    int io_class = io_prio >> IOPRIO_CLASS_SHIFT;

    io_prio &= ((1 << IOPRIO_CLASS_SHIFT) - 1);

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

inline int ioprio_value(int class, int prio)
{
    return (class << IOPRIO_CLASS_SHIFT) | prio;
}

inline int set_ioprio(int which, int who, int ioprio_class, int ioprio_prio)
{
    return syscall(SYS_ioprio_set, which, who, ioprio_value(ioprio_class, ioprio_prio));
}

