/* SPDX-License-Identifer: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020  Boian Bonev

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

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
