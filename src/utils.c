/*

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020  Boian Bonev

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

#include "iotop.h"

#include <time.h>
#include <fcntl.h>
#include <stdio.h>
#include <wchar.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

inline char *read_cmdline2(int pid)
{
    char *rv = NULL;
    char path[30];
    int fd;

    sprintf(path, "/proc/%d/cmdline", pid);
    fd = open(path, O_RDONLY);
    if (fd != -1)
    {
        char *dbuf=malloc(BUFSIZ + 1);
        size_t n, p = 0, sz = BUFSIZ;

        if (!dbuf)
        {
            close(fd);
            return NULL;
        }

        do
        {
            n = read(fd, dbuf + p, sz - p);
            if (n == sz - p)
            {
                char *t = realloc(dbuf, sz + BUFSIZ + 1);

                if (!t)
                {
                    close(fd);
                    free(dbuf);
                    return NULL;
                }
                dbuf = t;
                sz += BUFSIZ;
            }
            if (n > 0)
                 p += n;
        }
        while (n > 0);

        if (p > 0)
        {
            dbuf[p] = 0;
            if (!config.f.fullcmdline && (
                dbuf[0] == '/' ||
                (p > 1 && dbuf[0] == '.' && dbuf[1] == '/') ||
                (p > 2 && dbuf[0] == '.' && dbuf[1] == '.' && dbuf[2] == '/')))
            {
                char *ep;

                ep = strrchr(dbuf, '/');
                if (ep && ep[1])
                {
                    char *t = strdup(ep + 1);

                    if (t)
                    {
                        free(dbuf);
                        dbuf = t;
                        p = strlen(t) + 1;
                    }
                }
            }

            if (config.f.fullcmdline)
            {
                size_t k;

                for (k = 0; k < p; k++)
                    dbuf[k] = dbuf[k] ? dbuf[k] : ' ';
            }
            rv = dbuf;
        }
        else
            free(dbuf);
        close(fd);
    }

    if (rv)
        return rv;

    sprintf(path, "/proc/%d/status", pid);
    fd = open(path, O_RDONLY);
    if (fd != -1)
    {
        char buf[BUFSIZ + 1];
        size_t n = read(fd, buf, BUFSIZ);

        close(fd);

        if (n > 0)
        {
            char *eol, *tab;

            buf[n] = 0;
            eol = strchr(buf, '\n');
            tab = strchr(buf, '\t');
            if (eol && tab && eol > tab)
            {
                eol[0] = 0;
                rv = malloc(strlen(tab + 1) + 2 + 1);
                if (rv)
                    sprintf(rv, config.f.fullcmdline ? "[%s]" : "%s", tab + 1);
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

#define UBLEN 1024

inline char *u8strpadt(const char *s, size_t len)
{
    char *d = malloc(UBLEN);
    size_t dl = UBLEN;
    size_t si = 0;
    size_t di = 0;
    size_t tl = 0;
    size_t sl;
    wchar_t w;

    if (!d)
        return NULL;
    if (!s)
        s = "(null)";

    sl = strlen(s);
    mbtowc(NULL, NULL, 0);
    for (;;)
    {
        int cl;
        int tw;

        if (!s[si])
            break;

        cl = mbtowc(&w, s + si, sl - si);
        if (cl <= 0)
        {
            si++;
            continue;
        }
        if (dl - di < (size_t)cl + 1)
        {
            char *t;

            dl += UBLEN;
            t = realloc(d, dl);
            if (!t)
            {
                free(d);
                return NULL;
            }
            d = t;
        }
        tw = wcwidth(w);
        if (tw < 0)
        {
            si += cl;
            continue;
        }
        if (tw && tw + tl > len)
            break;
        memcpy(d + di, s + si, cl);
        di += cl;
        si += cl;
        tl += tw;
        d[di] = 0;
    }
    while (tl < len)
    {
        if (dl - di < 1 + 1)
        {
            char *t;

            dl += UBLEN;
            t = realloc(d, dl);
            if (!t)
            {
                free(d);
                return NULL;
            }
            d = t;
        }
        d[di++] = ' ';
        d[di] = 0;
        tl++;
    }
    return d;
}

