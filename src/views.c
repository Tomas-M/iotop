/* SPDX-License-Identifer: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020  Boian Bonev

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

#include "iotop.h"

// allow ncurses printf-like arguments checking
#define GCC_PRINTF

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curses.h>
#include <locale.h>
#include <langinfo.h>
#include <sys/time.h>

#define HEADER1_FORMAT "  Total DISK READ:   %7.2f %s%s |   Total DISK WRITE:   %7.2f %s%s"
#define HEADER2_FORMAT "Current DISK READ:   %7.2f %s%s | Current DISK WRITE:   %7.2f %s%s"

static uint64_t xxx = ~0ULL;
static int in_ionice = 0;
static char ionice_id[50];
static int ionice_cl = 1; // select what to edit class(1) or prio(0)
static int ionice_class = IOPRIO_CLASS_RT;
static int ionice_prio = 0;
static int ionice_id_changed = 0;
static int has_unicode = 0;
static int unicode = 1;
static double hist_t_r[HISTORY_CNT] = {0};
static double hist_t_w[HISTORY_CNT] = {0};
static double hist_a_r[HISTORY_CNT] = {0};
static double hist_a_w[HISTORY_CNT] = {0};

#define RRV(to, from) (((to) < (from)) ? (xxx) - (to) + (from) : (to) - (from))
#define RRVf(pto, pfrom, fld) RRV(pto->fld, pfrom->fld)
#define TIMEDIFF_IN_S(sta, end) ((((sta) == (end)) || (sta) == 0) ? 0.0001 : (((end) - (sta)) / 1000.0))
#define SORT_CHAR(x) ((sort_by == x) ? (sort_order == SORT_ASC ? '<' : '>') : ' ')

enum
{
    SORT_BY_PID,
    SORT_BY_PRIO,
    SORT_BY_USER,
    SORT_BY_READ,
    SORT_BY_WRITE,
    SORT_BY_SWAPIN,
    SORT_BY_IO,
    SORT_BY_COMMAND,
    SORT_BY_MAX
};

enum
{
    SORT_DESC,
    SORT_ASC
};

static const char *column_name[] =
{
    "xID", // unused, value varies - PID or TID
    "PRIO",
    "USER",
    "DISK READ",
    "DISK WRITE",
    "SWAPIN",
    "IO",
    "COMMAND",
};

// Braille unicode pseudo graph - 2x5 levels graph per character
static const char *br_graph[5][5] =
{
    {"⠀", "⢀", "⢠", "⢰", "⢸", },
    {"⡀", "⣀", "⣠", "⣰", "⣸", },
    {"⡄", "⣄", "⣤", "⣴", "⣼", },
    {"⡆", "⣆", "⣦", "⣶", "⣾", },
    {"⡇", "⣇", "⣧", "⣷", "⣿", },
};

// ASCII pseudo graph - 1x5 levels graph per character
static const char *as_graph[5] = {" ", "_", ".", ":", "|", };

static const char *column_format[] =
{
    "%s%c%*s ",
    "%4s%c%*s  ",
    "%6s%c%*s   ",
    "%11s%c%*s ",
    "%11s%c%*s ",
    "%7s%c%*s ",
    "%7s%c%*s ",
    "%s%c%*s",
};

static int maxpidlen = 5;

#define __COLUMN_NAME(i) ((i) == 0 ? (config.f.processes ? "PID" : "TID") : column_name[(i)])
#define __COLUMN_FORMAT(i) (column_format[(i)])
#define __SAFE_INDEX(i) ((((i) % SORT_BY_MAX) + SORT_BY_MAX) % SORT_BY_MAX)
#define COLUMN_NAME(i) __COLUMN_NAME(__SAFE_INDEX(i))
#define COLUMN_FORMAT(i) __COLUMN_FORMAT(__SAFE_INDEX(i))
#define COLUMN_L(i) COLUMN_NAME((i) - 1)
#define COLUMN_R(i) COLUMN_NAME((i) + 1)

static int sort_by = SORT_BY_IO;
static int sort_order = SORT_DESC;


static inline int value2scale(double val, double mx)
{
    val = 100.0 * val / mx;

    if (val > 75)
        return 4;
    if (val > 50)
        return 3;
    if (val > 25)
        return 2;
    if (val > 0)
        return 1;
    return 0;
}

inline int create_diff(struct xxxid_stats_arr *cs, struct xxxid_stats_arr *ps, double time_s)
{
    int diff_size = cs->length;
    int n = 0;

    for (n = 0; cs->arr && n < cs->length; n++)
    {
        struct xxxid_stats *c;
        struct xxxid_stats *p;
        double rv, wv;
        char temp[12];

        c = cs->arr[n];
        p = arr_find(ps, c->tid);

        if (!p)
        {
            // new process or task
            c->blkio_val = 0;
            c->swapin_val = 0;
            c->read_val = 0;
            c->write_val = 0;
            c->read_val_acc = 0;
            c->write_val_acc = 0;

            sprintf(temp, "%i", c->tid);
            maxpidlen = maxpidlen < (int)strlen(temp) ? (int)strlen(temp) : maxpidlen;
            continue;
        }

        // round robin value
        c->blkio_val =
            (double) RRVf(c, p, blkio_delay_total) / (time_s * 10000000.0);
        if (c->blkio_val > 100)
            c->blkio_val = 100;

        c->swapin_val =
            (double) RRVf(c, p, swapin_delay_total) / (time_s * 10000000.0);
        if (c->swapin_val > 100)
            c->swapin_val = 100;

        rv = (double) RRVf(c, p, read_bytes);
        wv = (double) RRVf(c, p, write_bytes);

        c->read_val = rv / time_s;
        c->write_val = wv / time_s;

        c->read_val_acc = p->read_val_acc + rv;
        c->write_val_acc = p->write_val_acc + wv;

        memcpy(c->iohist + 1, p->iohist, sizeof c->iohist - sizeof *c->iohist);
        c->iohist[0] = value2scale(c->blkio_val, 100.0);

        sprintf(temp, "%i", c->tid);
        maxpidlen = maxpidlen < (int)strlen(temp) ? (int)strlen(temp) : maxpidlen;
    }

    return diff_size;
}

inline void calc_total(struct xxxid_stats_arr *cs, double *read, double *write)
{
    int i;

    *read = *write = 0;

    for (i = 0; i < cs->length; i++)
    {
        if (!config.f.accumulated)
        {
            *read += cs->arr[i]->read_val;
            *write += cs->arr[i]->write_val;
        }
        else
        {
            *read += cs->arr[i]->read_val_acc;
            *write += cs->arr[i]->write_val_acc;
        }
    }
}

inline void calc_a_total(struct act_stats *act, double *read, double *write, double time_s)
{
    *read = *write = 0;

    if (act->have_o)
    {
        uint64_t r = act->read_bytes;
        uint64_t w = act->write_bytes;

        r = RRV(r, act->read_bytes_o);
        w = RRV(w, act->write_bytes_o);
        *read = (double) r / time_s;
        *write = (double) w / time_s;
    }
}

static inline void humanize_val(double *value, char **str, int allow_accum)
{
    static char *prefix_acc[] = {"B  ", "K  ", "M  ", "G  ", "T  "};
    static char *prefix[] = {"B/s", "K/s", "M/s", "G/s", "T/s"};
    int p;

    if (config.f.kilobytes)
    {
        *value /= 1000.0;
        *str = config.f.accumulated && allow_accum ? prefix_acc[1] : prefix[1];
        return;
    }

    p = 0;
    while (*value > 10000 && p < 5)
    {
        *value /= 1000.0;
        p++;
    }

    *str = config.f.accumulated && allow_accum ? prefix_acc[p] : prefix[p];
}

static inline int my_sort_cb(const void *a,const void *b,void *arg)
{
    int order = (((long)arg) & 1) ? 1 : -1; // SORT_ASC is bit 0=1, else should reverse sort
    struct xxxid_stats **ppa = (struct xxxid_stats **)a;
    struct xxxid_stats **ppb = (struct xxxid_stats **)b;
    struct xxxid_stats *pa = *ppa;
    struct xxxid_stats *pb = *ppb;
    int type = ((long)arg) >> 1;
    int res = 0;

    switch (type)
    {
        case SORT_BY_PRIO:
            res = pa->io_prio - pb->io_prio;
            break;
        case SORT_BY_COMMAND:
            res = strcmp(pa->cmdline, pb->cmdline);
            break;
        case SORT_BY_PID:
            res = pa->tid - pb->tid;
            break;
        case SORT_BY_USER:
            res = strcmp(pa->pw_name, pb->pw_name);
            break;
        case SORT_BY_READ:
            if (config.f.accumulated)
                res = pa->read_val_acc - pb->read_val_acc;
            else
                res = pa->read_val - pb->read_val;
            break;
        case SORT_BY_WRITE:
            if (config.f.accumulated)
                res = pa->write_val_acc - pb->write_val_acc;
            else
                res = pa->write_val - pb->write_val;
            break;
        case SORT_BY_SWAPIN:
            res = pa->swapin_val - pb->swapin_val;
            break;
        case SORT_BY_IO:
            res = pa->blkio_val - pb->blkio_val;
            break;
    }
    res *= order;
    return res;
}

inline void view_batch(struct xxxid_stats_arr *cs, struct xxxid_stats_arr *ps, struct act_stats *act)
{
    double time_s = TIMEDIFF_IN_S(act->ts_o, act->ts_c);
    int diff_len = create_diff(cs, ps, time_s);
    double total_read, total_write;
    double total_a_read, total_a_write;
    char *str_read, *str_write;
    char *str_a_read, *str_a_write;
    int i;

    calc_total(cs, &total_read, &total_write);
    calc_a_total(act, &total_a_read, &total_a_write, time_s);

    humanize_val(&total_read, &str_read, 1);
    humanize_val(&total_write, &str_write, 1);
    humanize_val(&total_a_read, &str_a_read, 0);
    humanize_val(&total_a_write, &str_a_write, 0);

    printf(HEADER1_FORMAT,
           total_read,
           str_read,
           "",
           total_write,
           str_write,
           ""
          );

    if (config.f.timestamp)
    {
        time_t t = time(NULL);

        printf(" | %s", ctime(&t));
    }
    else
        printf("\n");

    printf(HEADER2_FORMAT,
           total_a_read,
           str_a_read,
           "",
           total_a_write,
           str_a_write,
           ""
          );

    printf("\n");

    if (!config.f.quiet)
        printf("%5s %4s %8s %11s %11s %6s %6s %s\n",
               config.f.processes ? "PID" : "TID",
               "PRIO",
               "USER",
               "DISK READ",
               "DISK WRITE",
               "SWAPIN",
               "IO",
               "COMMAND"
              );

    arr_sort(cs,my_sort_cb,(void *)(long)SORT_ASC);

    for (i = 0; cs->sor && i < diff_len; i++)
    {
        struct xxxid_stats *s = cs->sor[i];
        double read_val = config.f.accumulated ? s->read_val_acc : s->read_val;
        double write_val = config.f.accumulated ? s->write_val_acc : s->write_val;
        char *read_str, *write_str;
        char *pw_name;

        if (config.f.only && !read_val && !write_val)
            continue;

        humanize_val(&read_val, &read_str, 1);
        humanize_val(&write_val, &write_str, 1);

        pw_name = u8strpadt(s->pw_name, 10);

        printf("%5i %4s %s %7.2f %-3.3s %7.2f %-3.3s %2.2f %% %2.2f %% %s\n",
               s->tid,
               str_ioprio(s->io_prio),
               pw_name ? pw_name : "(null)",
               read_val,
               read_str,
               write_val,
               write_str,
               s->swapin_val,
               s->blkio_val,
               s->cmdline
              );

        if (pw_name)
            free(pw_name);
    }
}

inline void view_curses(struct xxxid_stats_arr *cs, struct xxxid_stats_arr *ps, struct act_stats *act)
{
    double time_s = TIMEDIFF_IN_S(act->ts_o, act->ts_c);
    static const uint8_t iohist_z[HISTORY_CNT] = {0};
    int diff_len = create_diff(cs, ps, time_s);
    double total_read, total_write;
    double total_a_read, total_a_write;
    char pg_t_r[HISTORY_POS * 5] = {0};
    char pg_t_w[HISTORY_POS * 5] = {0};
    char pg_a_r[HISTORY_POS * 5] = {0};
    char pg_a_w[HISTORY_POS * 5] = {0};
    char *str_read, *str_write;
    char *str_a_read, *str_a_write;
    int promptx = 0, prompty = 0, show;
    double mx_t_r = 1000.0;
    double mx_t_w = 1000.0;
    double mx_a_r = 1000.0;
    double mx_a_w = 1000.0;
    int line, lastline;
    int gr_width;
    int maxy;
    int maxx;
    int i, j;

    if (!stdscr)
    {
        if (strcmp(getenv("TERM"), "linux"))
        {
            if (setlocale(LC_CTYPE, "C.UTF-8"))
                has_unicode = 1;
            else
                if (setlocale(LC_CTYPE, ""))
                    if (!strcmp("UTF-8", nl_langinfo(CODESET)))
                        has_unicode = 1;
        }
        initscr();
        keypad(stdscr, TRUE);
        nonl();
        cbreak();
        noecho();
        curs_set(FALSE);
        nodelay(stdscr, TRUE);
    }

    maxy = getmaxy(stdscr);
    maxx = getmaxx(stdscr);

    calc_total(cs, &total_read, &total_write);
    calc_a_total(act, &total_a_read, &total_a_write, time_s);

    memmove(hist_t_r + 1, hist_t_r, sizeof hist_t_r - sizeof *hist_t_r);
    memmove(hist_t_w + 1, hist_t_w, sizeof hist_t_w - sizeof *hist_t_w);
    memmove(hist_a_r + 1, hist_a_r, sizeof hist_a_r - sizeof *hist_a_r);
    memmove(hist_a_w + 1, hist_a_w, sizeof hist_a_w - sizeof *hist_a_w);
    hist_t_r[0] = total_read;
    hist_t_w[0] = total_write;
    hist_a_r[0] = total_a_read;
    hist_a_w[0] = total_a_write;

    humanize_val(&total_read, &str_read, 1);
    humanize_val(&total_write, &str_write, 1);
    humanize_val(&total_a_read, &str_a_read, 0);
    humanize_val(&total_a_write, &str_a_write, 0);

    gr_width = maxx - 5 - 2 - 4 - 2 - 9 - 7 - 1 - 3 - 2 - 7 - 1 - 3 - 1 - 5 - 3 - 5 - 4 - 2;
    gr_width /= 4;
    if (gr_width < 5)
        gr_width = 5;
    if (gr_width > HISTORY_POS)
        gr_width = HISTORY_POS;

    for (i = 0; i < ((has_unicode && unicode) ? gr_width * 2 : gr_width); i++)
    {
        if (mx_t_r < hist_t_r[i])
            mx_t_r = hist_t_r[i];
        if (mx_t_w < hist_t_w[i])
            mx_t_w = hist_t_w[i];
        if (mx_a_r < hist_a_r[i])
            mx_a_r = hist_a_r[i];
        if (mx_a_w < hist_a_w[i])
            mx_a_w = hist_a_w[i];
    }
    strcpy(pg_t_r, " ");
    strcpy(pg_t_w, " ");
    strcpy(pg_a_r, " ");
    strcpy(pg_a_w, " ");
    for (j = 0; j < gr_width; j++)
    {
        if (has_unicode && unicode)
        {
            sprintf(pg_t_r + strlen(pg_t_r), "%s",
                br_graph[value2scale(hist_t_r[j * 2], mx_t_r)]
                        [value2scale(hist_t_r[j * 2 + 1], mx_t_r)]);
            sprintf(pg_t_w + strlen(pg_t_w), "%s",
                br_graph[value2scale(hist_t_w[j * 2], mx_t_w)]
                        [value2scale(hist_t_w[j * 2 + 1], mx_t_w)]);
            sprintf(pg_a_r + strlen(pg_a_r), "%s",
                br_graph[value2scale(hist_a_r[j * 2], mx_a_r)]
                        [value2scale(hist_a_r[j * 2 + 1], mx_a_r)]);
            sprintf(pg_a_w + strlen(pg_a_w), "%s",
                br_graph[value2scale(hist_a_w[j * 2], mx_a_w)]
                        [value2scale(hist_a_w[j * 2 + 1], mx_a_w)]);
        }
        else
        {
            sprintf(pg_t_r + strlen(pg_t_r), "%s",
                as_graph[value2scale(hist_t_r[j], mx_t_r)]);
            sprintf(pg_t_w + strlen(pg_t_w), "%s",
                as_graph[value2scale(hist_t_w[j], mx_t_w)]);
            sprintf(pg_a_r + strlen(pg_a_r), "%s",
                as_graph[value2scale(hist_a_r[j], mx_a_r)]);
            sprintf(pg_a_w + strlen(pg_a_w), "%s",
                as_graph[value2scale(hist_a_w[j], mx_a_w)]);
        }
    }

    mvhline(0, 0, ' ', maxx);
    mvprintw(0, 0, HEADER1_FORMAT,
             total_read,
             str_read,
             config.f.iohist ? pg_t_r : "",
             total_write,
             str_write,
             config.f.iohist ? pg_t_w : ""
            );

    mvhline(1, 0, ' ', maxx);
    if (!in_ionice)
    {
        mvprintw(1, 0, HEADER2_FORMAT,
                 total_a_read,
                 str_a_read,
                 config.f.iohist ? pg_a_r : "",
                 total_a_write,
                 str_a_write,
                 config.f.iohist ? pg_a_w : ""
                );
        show = FALSE;
    }
    else
    {
        mvprintw(1, 0, "%s: ", COLUMN_NAME(0));
        attron(A_BOLD);
        printw("%s", ionice_id);
        attroff(A_BOLD);
        getyx(stdscr, promptx, prompty);

        if (strlen(ionice_id))
        {
            struct xxxid_stats *p = NULL;
            pid_t id = atoi(ionice_id);

            if (id && (p = arr_find(cs, id)))
            {
                printw(" Current: ");
                attron(A_BOLD);
                printw("%s", str_ioprio(p->io_prio));
                attroff(A_BOLD);
                printw(" Change to: ");

                if (ionice_id_changed)
                {
                    ionice_id_changed = 0;
                    ionice_class = ioprio2class(p->io_prio);
                    ionice_prio = ioprio2prio(p->io_prio);
                }

                attron(A_BOLD);
                if (ionice_cl)
                    attron(A_REVERSE);
                printw("%s", str_ioprio_class[ionice_class]);
                if (ionice_cl)
                    attroff(A_REVERSE);
                printw("/");
                if (!ionice_cl)
                    attron(A_REVERSE);
                printw("%d", ionice_prio);
                if (!ionice_cl)
                    attroff(A_REVERSE);
                attroff(A_BOLD);
            }
            else
                printw(" (invalid %s)", COLUMN_NAME(0));
        }
        else
            printw(" (select %s)", COLUMN_NAME(0));
        printw(" ");
        attron(A_REVERSE);
        printw("[use 0-9/bksp for %s, tab and arrows for prio]", COLUMN_NAME(0));
        attroff(A_REVERSE);
        show = TRUE;
    }

    attron(A_REVERSE);
    mvhline(2, 0, ' ', maxx);
    move(2, 0);

    for (i = 0; i < SORT_BY_MAX; i++)
    {
        int vwidth = 0;

        if (i == 0)
            vwidth = maxpidlen - strlen(COLUMN_NAME(0));
        if (i == 6)
            vwidth = config.f.iohist ? gr_width : 0;

        if (sort_by == i)
            attron(A_BOLD);
        printw(COLUMN_FORMAT(i), COLUMN_NAME(i), SORT_CHAR(i), vwidth, "");
        if (sort_by == i)
            attroff(A_BOLD);
    }
    attroff(A_REVERSE);

    arr_sort(cs,my_sort_cb,(void *)(long)(sort_by * 2 + !!(sort_order == SORT_ASC)));

    line = 3;
    lastline = line;
    for (i = 0; cs->sor && i < diff_len; i++)
    {
        struct xxxid_stats *s = cs->sor[i];
        double read_val = config.f.accumulated ? s->read_val_acc : s->read_val;
        double write_val = config.f.accumulated ? s->write_val_acc : s->write_val;
        char iohist[HISTORY_POS * 5];
        char *read_str, *write_str;
        char *pw_name, *cmdline;
        int maxcmdline;

        // visible history is non-zero
        if (config.f.only && memcmp(s->iohist, iohist_z, (has_unicode && unicode) ? gr_width * 2 : gr_width))
            continue;

        humanize_val(&read_val, &read_str, 1);
        humanize_val(&write_val, &write_str, 1);

        maxcmdline = maxx - 5 - 2 - 4 - 2 - 9 - 7 - 1 - 3 - 2 - 7 - 1 - 3 - 1 - 5 - 3 - 5 - 4 - 2;
        if (config.f.iohist)
            maxcmdline -= gr_width;
        if (maxcmdline < 0)
            maxcmdline = 0;

        pw_name = u8strpadt(s->pw_name, 9);
        cmdline = u8strpadt(s->cmdline, maxcmdline);

        if (config.f.iohist)
        {
            int j;

            for (j = 0; j < gr_width; j++)
                if (has_unicode && unicode)
                    sprintf(iohist + (j ? strlen(iohist) : 0), "%s",
                        br_graph[s->iohist[j * 2]][s->iohist[j * 2 + 1]]);
            else
                    sprintf(iohist + (j ? strlen(iohist) : 0), "%s",
                        as_graph[s->iohist[j]]);
        }

        mvprintw(line, 0, "%*i  %4s  %s  %7.2f %-3.3s  %7.2f %-3.3s %6.2f %% %6.2f %% %s %s\n",
                 maxpidlen,
                 s->tid,
                 str_ioprio(s->io_prio),
                 pw_name ? pw_name : "(null)",
                 read_val,
                 read_str,
                 write_val,
                 write_str,
                 s->swapin_val,
                 s->blkio_val,
                 config.f.iohist ? iohist : "",
                 cmdline
                );

        if (pw_name)
            free(pw_name);
        if (cmdline)
            free(cmdline);

        line++;
        lastline = line;
        if (line > maxy - (config.f.nohelp ? 1 : 3)) // do not draw out of screen, keep 2 lines for help
            break;
    }
    for (line = lastline; line <= maxy - (config.f.nohelp ? 1 : 3); line++) // always draw empty lines
        mvhline(line, 0, ' ', maxx);

    if (!config.f.nohelp)
    {
        attron(A_REVERSE);

        mvhline(maxy - 2, 0, ' ', maxx);
        mvhline(maxy - 1, 0, ' ', maxx);
        mvprintw(maxy - 2, 0, "%s", "  keys:  any: refresh  ");
        attron(A_UNDERLINE);
        printw("q");
        attroff(A_UNDERLINE);
        printw(": quit  ");
        attron(A_UNDERLINE);
        printw("i");
        attroff(A_UNDERLINE);
        printw(": ionice  ");
        attron(A_UNDERLINE);
        printw("o");
        attroff(A_UNDERLINE);
        printw(": %s  ", config.f.only ? "all" : "active");
        attron(A_UNDERLINE);
        printw("p");
        attroff(A_UNDERLINE);
        printw(": %s  ", config.f.processes ? "threads" : "procs");
        attron(A_UNDERLINE);
        printw("a");
        attroff(A_UNDERLINE);
        printw(": %s  ", config.f.accumulated ? "bandwidth" : "accum");
        attron(A_UNDERLINE);
        printw("s");
        attroff(A_UNDERLINE);
        printw(": %s  ", config.f.iohist ? "no-graph" : "graph");
        if (has_unicode)
        {
            attron(A_UNDERLINE);
            printw("u");
            attroff(A_UNDERLINE);
            printw(": %s  ", unicode ? "ASCII" : "UTF");
        }
        attron(A_UNDERLINE);
        printw("h");
        attroff(A_UNDERLINE);
        printw(": help");

        mvprintw(maxy - 1, 0, "  sort:  ");
        attron(A_UNDERLINE);
        printw("r");
        attroff(A_UNDERLINE);
        printw(": %s  ", sort_order == SORT_ASC ? "desc" : "asc");
        attron(A_UNDERLINE);
        printw("left");
        attroff(A_UNDERLINE);
        printw(": %s  ", COLUMN_L(sort_by));
        attron(A_UNDERLINE);
        printw("right");
        attroff(A_UNDERLINE);
        printw(": %s  ", COLUMN_R(sort_by));
        attron(A_UNDERLINE);
        printw("home");
        attroff(A_UNDERLINE);
        printw(": %s  ", COLUMN_L(1));
        attron(A_UNDERLINE);
        printw("end");
        attroff(A_UNDERLINE);
        printw(": %s  ", COLUMN_L(0));

        attroff(A_REVERSE);
    }

    if (show)
        move(promptx, prompty);
    curs_set(show);
    refresh();
}

inline void view_curses_finish(void)
{
    endwin();
}

inline unsigned int curses_sleep(unsigned int seconds)
{
    struct timeval tv;
    fd_set fds;
    int rv;

    FD_ZERO(&fds);
    FD_SET(fileno(stdin), &fds);

    tv.tv_sec = seconds;
    tv.tv_usec = 0;

    rv = select(1, &fds, NULL, NULL, &tv);
    if (rv)
    {
        int ch;

        switch ((ch = getch()))
        {
        case 'q':
        case 'Q':
            if (in_ionice)
            {
                in_ionice = 0;
                return 0;
            }
            return 1;
        case ' ':
        case 'r':
        case 'R':
            sort_order = (sort_order == SORT_ASC) ? SORT_DESC : SORT_ASC;
            return 0;
        case KEY_HOME:
            if (in_ionice)
                ionice_cl = 1;
            else
                sort_by = 0;
            return 0;
        case KEY_END:
            if (in_ionice)
                ionice_cl = 0;
            else
                sort_by = SORT_BY_MAX - 1;
            return 0;
        case KEY_RIGHT:
            if (in_ionice)
                ionice_cl = !ionice_cl;
            else
                if (++sort_by == SORT_BY_MAX)
                    sort_by = SORT_BY_PID;
            return 0;
        case KEY_LEFT:
            if (in_ionice)
                ionice_cl = !ionice_cl;
            else
                if (--sort_by == -1)
                    sort_by = SORT_BY_MAX - 1;
            return 0;
        case KEY_UP:
            if (in_ionice)
            {
                if (ionice_cl)
                {
                    ionice_class++;
                    if (ionice_class >= IOPRIO_CLASS_MAX)
                        ionice_class = IOPRIO_CLASS_MIN;
                }
                else
                {
                    ionice_prio++;
                    if (ionice_prio > 7)
                        ionice_prio = 0;
                }
            }
            break;
        case KEY_DOWN:
            if (in_ionice)
            {
                if (ionice_cl)
                {
                    ionice_class--;
                    if (ionice_class < IOPRIO_CLASS_MIN)
                        ionice_class = IOPRIO_CLASS_MAX - 1;
                }
                else
                {
                    ionice_prio--;
                    if (ionice_prio < 0)
                        ionice_prio = 7;
                }
            }
            break;
        case 'o':
        case 'O':
            config.f.only = !config.f.only;
            return 0;
        case 'p':
        case 'P':
            config.f.processes = !config.f.processes;
            return 0;
        case 'a':
        case 'A':
            config.f.accumulated = !config.f.accumulated;
            return 0;
        case '?':
        case 'h':
        case 'H':
            config.f.nohelp = !config.f.nohelp;
            return 0;
        case 'c':
        case 'C':
            config.f.fullcmdline = !config.f.fullcmdline;
            return 0;
        case 's':
        case 'S':
            config.f.iohist = !config.f.iohist;
            return 0;
        case 'i':
        case 'I':
            in_ionice = 1;
            ionice_id[0] = 0;
            ionice_cl = 1;
            ionice_id_changed = 1;
            return 0;
        case 'u':
        case 'U':
            unicode = !unicode;
            return 0;
        case 27: // ESC
            in_ionice = 0;
            break;
        case '\r': // CR
        case KEY_ENTER:
            if (in_ionice) {
                pid_t pgid = atoi(ionice_id);
                int who = IOPRIO_WHO_PROCESS;

                if (config.f.processes)
                {
                    pgid = getpgid(pgid);
                    who = IOPRIO_WHO_PGRP;
                }
                in_ionice = 0;
                set_ioprio(who, pgid, ionice_class, ionice_prio);
            }
            break;
        case '\t': // TAB
            if (in_ionice)
                ionice_cl = !ionice_cl;
            break;
        case KEY_BACKSPACE:
            if (in_ionice == 1)
            {
                int idlen = strlen(ionice_id);

                if (idlen)
                {
                    ionice_id[idlen - 1] = 0;
                    ionice_id_changed = 1;
                }
            }
            break;
        case '0' ... '9':
            if (in_ionice == 1)
            {
                size_t idlen = strlen(ionice_id);

                if (idlen < sizeof ionice_id - 1)
                {
                    ionice_id[idlen++] = ch;
                    ionice_id[idlen] = 0;
                    ionice_id_changed = 1;
                }
            }
            break;
        }
    }

    return 0;
}
