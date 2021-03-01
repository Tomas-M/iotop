/* SPDX-License-Identifer: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020,2021  Boian Bonev

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

#include "iotop.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define TIMEDIFF_IN_S(sta,end) ((((sta)==(end))||(sta)==0)?0.0001:(((end)-(sta))/1000.0))

static inline void view_batch(struct xxxid_stats_arr *cs,
                              struct xxxid_stats_arr *ps, struct act_stats *act)
{
    double time_s = TIMEDIFF_IN_S(act->ts_o, act->ts_c);
    int diff_len = create_diff(cs, ps, time_s, NULL, 0, NULL);
    double total_a_read, total_a_write;
    char str_a_read[4], str_a_write[4];
    double total_read, total_write;
    char str_read[4], str_write[4];
    int i;

    calc_total(cs, &total_read, &total_write);
    calc_a_total(act, &total_a_read, &total_a_write, time_s);

    humanize_val(&total_read, str_read, 1);
    humanize_val(&total_write, str_write, 1);
    humanize_val(&total_a_read, str_a_read, 0);
    humanize_val(&total_a_write, str_a_write, 0);

    printf(HEADER1_FORMAT, total_read, str_read, "", total_write, str_write,
           "");

    if (config.f.timestamp) {
        time_t t = time(NULL);

        printf(" | %s", ctime(&t));
    } else
        printf("\n");

    printf(HEADER2_FORMAT, total_a_read, str_a_read, "", total_a_write,
           str_a_write, "");

    printf("\n");

    if (!config.f.quiet)
        printf("%6s %4s %8s %11s %11s %6s %6s %s\n",
               config.f.processes ? "PID" : "TID", "PRIO", "USER", "DISK READ",
               "DISK WRITE", "SWAPIN", "IO", "COMMAND");
    arr_sort(cs, iotop_sort_cb);

    for (i = 0; cs->sor && i < diff_len; i++) {
        struct xxxid_stats *s = cs->sor[i];
        double read_val = config.f.accumulated ? s->read_val_acc : s->read_val;
        double write_val =
            config.f.accumulated ? s->write_val_acc : s->write_val;
        char read_str[4], write_str[4];
        char *pw_name;

        // show only processes, if configured
        if (config.f.processes && s->pid != s->tid)
            continue;
        if (config.f.only && !read_val && !write_val)
            continue;
        if (s->exited)          // do not show exited processes in batch view
            continue;

        humanize_val(&read_val, read_str, 1);
        humanize_val(&write_val, write_str, 1);

        pw_name = u8strpadt(s->pw_name, 10);

        printf("%6i %4s %s %7.2f %-3.3s %7.2f %-3.3s %2.2f %% %2.2f %% %s\n",
               s->tid, str_ioprio(s->io_prio), pw_name ? pw_name : "(null)",
               read_val, read_str, write_val, write_str, s->swapin_val,
               s->blkio_val, config.f.fullcmdline ? s->cmdline2 : s->cmdline1);

        if (pw_name)
            free(pw_name);
    }
}

inline void view_batch_init(void)
{
}

inline void view_batch_fini(void)
{
}

inline void view_batch_loop(void)
{
    struct xxxid_stats_arr *ps = NULL;
    struct xxxid_stats_arr *cs = NULL;
    struct act_stats act = { 0 };

    for (;;) {
        cs = fetch_data(filter1);
        get_vm_counters(&act.read_bytes, &act.write_bytes);
        act.ts_c = monotime();
        view_batch(cs, ps, &act);

        if (ps)
            arr_free(ps);

        ps = cs;
        act.read_bytes_o = act.read_bytes;
        act.write_bytes_o = act.write_bytes;
        act.ts_o = act.ts_c;
        act.have_o = 1;

        if ((params.iter > -1) && ((--params.iter) == 0))
            break;
        sleep(params.delay);
    }
    arr_free(cs);
}
