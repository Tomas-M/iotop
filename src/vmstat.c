#include "iotop.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PGIN "pgpgin "
#define PGOUT "pgpgout "

inline int get_vm_counters(uint64_t *pgpgin, uint64_t *pgpgout)
{
    char buf[200];
    int haveout = 0;
    int havein = 0;
    FILE *f;

    if (!pgpgin || !pgpgout)
        return EINVAL;

    if (NULL == (f = fopen("/proc/vmstat", "r")))
        return ENOENT;

    buf[sizeof buf - 1] = 0;
    while (!feof(f))
    {
        char *t;

        if (fgets(buf, sizeof buf - 1, f))
        {
            while (strlen(buf) && buf[strlen(buf) - 1] == '\n')
                buf[strlen(buf) - 1] = 0;
            if (!strncmp(buf, PGIN, strlen(PGIN)))
            {
                *pgpgin = 1024 * strtoull(buf + strlen(PGIN), &t, 10);
                if (!*t)
                    havein = 1;
            }
            if (!strncmp(buf, PGOUT, strlen(PGOUT)))
            {
                *pgpgout = 1024 * strtoull(buf + strlen(PGOUT), &t, 10);
                if (!*t)
                    haveout = 1;
            }
        }
        if (havein && haveout)
            break;
    }

    fclose(f);

    if (!havein || !haveout)
        return ENOENT;

    return 0;
}
