#include "iotop.h"

#include <sys/syscall.h>
#include <stdio.h>
#include <unistd.h>

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

inline int get_ioprio(pid_t pid)
{
    return syscall(SYS_ioprio_get, IOPRIO_WHO_PROCESS, pid);
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

