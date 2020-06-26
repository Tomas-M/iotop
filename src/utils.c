#include "iotop.h"

#include <time.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

inline char *read_cmdline2(int pid)
{
    static char buf[BUFSIZ + 1];
    char *rv = NULL;
    char path[30];
    FILE *fp;

    sprintf(path, "/proc/%d/cmdline", pid);
    fp = fopen(path, "rb");
    memset(buf, 0, BUFSIZ);
    if (fp)
    {
        size_t n = fread(buf, sizeof(char), BUFSIZ, fp);

        if (n > 0)
        {
            size_t k;
            char *ep;

            if (!config.f.fullcmdline)
            {
                ep = strrchr(buf, '/');
                if (ep && ep[1])
                {
                    memmove(buf, ep + 1, BUFSIZ - (ep - buf + 1));
                    n -= ep - buf + 1;
                }
            }

            for (k = 0; k < n - 1; k++)
                buf[k] = buf[k] ? buf[k] : ' ';
            rv = buf;
        }
        fclose(fp);
    }

    if (rv)
        return strdup(rv);

    sprintf(path, "/proc/%d/status", pid);
    fp = fopen(path, "rb");
    if (fp)
    {
        size_t n = fread(buf, sizeof(char), BUFSIZ, fp);
        char *eol, *tab;

        fclose(fp);

        if (n > 0)
        {
            buf[n] = 0;
            eol = strchr(buf, '\n');
            tab = strchr(buf, '\t');
            if (eol && tab && eol > tab)
            {
                eol[0] = 0;
                rv = malloc(strlen(tab + 1) + 2 + 1);
                if (rv)
                    sprintf(rv, "[%s]", tab + 1);
            }
        }
    }

    return rv;
}

static inline int __next_pid(DIR *dir)
{
    while (1)
    {
        struct dirent *de = readdir(dir);

        if (!de)
            return 0;

        char *eol = NULL;
        int pid = strtol(de->d_name, &eol, 10);

        if (*eol != '\0')
            continue;

        return pid;
    }

    return 0;
}

inline struct pidgen *openpidgen(int flags)
{
    struct pidgen *pg = malloc(sizeof(struct pidgen));

    if (!pg)
        return NULL;

    if ((pg->__proc = opendir("/proc")))
    {
        pg->__task = NULL;
        pg->__flags = flags;
        return pg;
    }

    free(pg);
    return NULL;
}

inline void closepidgen(struct pidgen *pg)
{
    if (pg->__proc)
        closedir((DIR *) pg->__proc);

    if (pg->__task)
        closedir((DIR *) pg->__task);

    free(pg);
}

inline int pidgen_next(struct pidgen *pg)
{
    int pid;

    if (pg->__task)
    {
        pid = __next_pid((DIR *) pg->__task);

        if (pid < 1)
        {
            closedir((DIR *) pg->__task);
            pg->__task = NULL;
            return pidgen_next(pg);
        }

        return pid;
    }

    pid = __next_pid((DIR *) pg->__proc);

    if (pid && (pg->__flags & PIDGEN_FLAGS_TASK))
    {
        char path[30];

        sprintf(path, "/proc/%d/task", pid);
        pg->__task = (DIR *) opendir(path);
        return pidgen_next(pg);
    }

    return pid;
}

inline int64_t monotime(void)
{
    struct timespec ts;
    int64_t res;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    res = ts.tv_sec * 1000;
    res += ts.tv_nsec / 1000000;
    return res;
}

