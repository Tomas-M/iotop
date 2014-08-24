#include "iotop.h"

#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *xprintf(const char *format, ...)
{
    static char buf[BUFSIZ];
    va_list args;

    memset(buf, 0, BUFSIZ);
    va_start(args, format);

    int j = vsnprintf(buf, BUFSIZ, format, args);

    va_end(args);

    return ((j >= 0) && (j < BUFSIZ)) ? buf : NULL;
}

const char *read_cmdline2(int pid)
{
    static char buf[BUFSIZ];
    FILE *fp = fopen(xprintf("/proc/%d/cmdline", pid), "rb");
    char *rv = NULL;

    memset(buf, 0, BUFSIZ);
    if (fp)
    {
        size_t n = fread(buf, sizeof(char), BUFSIZ, fp);
        if (n > 0)
        {
            size_t k;
            for (k = 0; k < n - 1; k++)
                buf[k] = buf[k] ? buf[k] : ' ';
            rv = buf;
        }
        fclose(fp);
    }

    if (rv)
        return rv;

    fp = fopen(xprintf("/proc/%d/status", pid), "rb");

    memset(buf, 0, BUFSIZ);
    if (fp)
    {
        size_t n = fread(buf, sizeof(char), BUFSIZ, fp);
        char *eol = NULL;

        if (n > 0 && (eol = strchr(buf, '\n') - 1)
                && (eol > strchr(buf, '\t')))
        {
            eol[0] = 0;
            strcpy(buf, xprintf("[%s]", strchr(buf, '\t') + 1));
            rv = buf;
        }
        fclose(fp);
    }

    return rv;
}

static int __next_pid(DIR *dir)
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

struct pidgen *openpidgen(int flags)
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

void closepidgen(struct pidgen *pg)
{
    if (pg->__proc)
        closedir((DIR *) pg->__proc);

    if (pg->__task)
        closedir((DIR *) pg->__task);

    free(pg);
}

int pidgen_next(struct pidgen *pg)
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
        pg->__task = (DIR *) opendir(xprintf("/proc/%d/task", pid));
        return pidgen_next(pg);
    }

    return pid;
}

