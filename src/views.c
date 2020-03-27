#include "iotop.h"

#include <curses.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>

#define HEADER1_FORMAT "  Total DISK READ:   %7.2f %s |   Total DISK WRITE:   %7.2f %s"
#define HEADER2_FORMAT "Current DISK READ:   %7.2f %s | Current DISK WRITE:   %7.2f %s"

static uint64_t xxx = ~0;

#define RRV(to, from) (((to) < (from)) ? (xxx) - (to) + (from) : (to) - (from))
#define RRVf(pto, pfrom, fld) RRV(pto->fld, pfrom->fld)

int create_diff(struct xxxid_stats_arr *cs, struct xxxid_stats_arr *ps)
{
    int diff_size = cs->length;
    int n = 0;

    for (n = 0; cs->arr && n < cs->length; n++)
    {
        struct xxxid_stats *c;
        struct xxxid_stats *p;

        c = cs->arr[n];
        p = arr_find(ps, c->tid);

        if (!p)
        {
            // new process or task
            c->read_bytes = 0;
            c->write_bytes = 0;
            c->swapin_delay_total = 0;
            c->blkio_delay_total = 0;
            continue;
        }

        // round robin value
        c->blkio_val =
            (double) RRVf(c, p, blkio_delay_total) / 10e9 / params.delay * 100;

        c->swapin_val =
            (double) RRVf(c, p, swapin_delay_total) / 10e9 / params.delay * 100;

        c->read_val = (double) RRVf(c, p, read_bytes)
                           / (config.f.accumulated ? 1 : params.delay);

        c->write_val = (double) RRVf(c, p, write_bytes)
                            / (config.f.accumulated ? 1 : params.delay);

        if (config.f.accumulated)
        {
            c->read_val += p->read_val;
            c->write_val += p->write_val;
        }
    }

    return diff_size;
}

void calc_total(struct xxxid_stats_arr *cs, double *read, double *write)
{
    int i;

    if (!config.f.accumulated)
        *read = *write = 0;

    for (i = 0; i < cs->length; i++)
    {
        *read += cs->arr[i]->read_val;
        *write += cs->arr[i]->write_val;
    }

    if (!config.f.accumulated)
    {
        *read /= params.delay;
        *write /= params.delay;
    }
}

void calc_a_total(struct act_stats *act, double *read, double *write)
{
    *read = *write = 0;

    if (act->have_o)
    {
        uint64_t r = act->read_bytes;
        uint64_t w = act->write_bytes;

        r = RRV(r, act->read_bytes_o);
        w = RRV(w, act->write_bytes_o);
        *read = (double) r / params.delay;
        *write = (double) w / params.delay;
    }
}

void humanize_val(double *value, char **str, int allow_accum)
{
    static char *prefix_acc[] = {"B  ", "K  ", "M  ", "G  ", "T  "};
    static char *prefix[] = {"B/s", "K/s", "M/s", "G/s", "T/s"};

    if (config.f.kilobytes)
    {
        *value /= 1000.0;
        *str = config.f.accumulated && allow_accum ? prefix_acc[1] : prefix[1];
        return;
    }

    int p = 0;
    while (*value > 10000 && p < 5)
    {
        *value /= 1000.0;
        p++;
    }

    *str = config.f.accumulated && allow_accum ? prefix_acc[p] : prefix[p];
}

void view_batch(struct xxxid_stats_arr *cs, struct xxxid_stats_arr *ps, struct act_stats *act)
{
    int diff_len = create_diff(cs, ps);
    struct xxxid_stats *s;

    static double total_read = 0, total_write = 0;
    double total_a_read, total_a_write;
    char *str_read, *str_write;
    char *str_a_read, *str_a_write;

    calc_total(cs, &total_read, &total_write);
    calc_a_total(act, &total_a_read, &total_a_write);

    humanize_val(&total_read, &str_read, 0);
    humanize_val(&total_write, &str_write, 0);
    humanize_val(&total_a_read, &str_a_read, 0);
    humanize_val(&total_a_write, &str_a_write, 0);

    printf(HEADER1_FORMAT,
           total_read,
           str_read,
           total_write,
           str_write
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
           total_a_write,
           str_a_write
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

    int i;

    for (i = 0; cs->sor && i < diff_len; i++)
    {
        s = cs->sor[i];

        double read_val = s->read_val;
        double write_val = s->write_val;

        if (config.f.only && !read_val && !write_val)
            continue;

        char *read_str, *write_str;

        humanize_val(&read_val, &read_str, 1);
        humanize_val(&write_val, &write_str, 1);

        printf("%5i %4s %-10.10s %7.2f %-3.3s %7.2f %-3.3s %2.2f %% %2.2f %% %s\n",
               s->tid,
               str_ioprio(s->io_prio),
               s->pw_name,
               read_val,
               read_str,
               write_val,
               write_str,
               s->swapin_val,
               s->blkio_val,
               s->cmdline
              );
    }
}

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

static const char *column_format[] =
{
    "%5s%c ",
    "%4s%c  ",
    "%6s%c   ",
    "%11s%c ",
    "%11s%c ",
    "%6s%c ",
    "%6s%c ",
    "%s%c",
};

#define __COLUMN_NAME(i) ((i) == 0 ? (config.f.processes ? "PID" : "TID") : column_name[(i)])
#define __COLUMN_FORMAT(i) (column_format[(i)])
#define __SAFE_INDEX(i) ((((i) % SORT_BY_MAX) + SORT_BY_MAX) % SORT_BY_MAX)
#define COLUMN_NAME(i) __COLUMN_NAME(__SAFE_INDEX(i))
#define COLUMN_FORMAT(i) __COLUMN_FORMAT(__SAFE_INDEX(i))
#define COLUMN_L(i) COLUMN_NAME((i) - 1)
#define COLUMN_R(i) COLUMN_NAME((i) + 1)

static int sort_by = SORT_BY_IO;
static int sort_order = SORT_DESC;

static int my_sort_cb(const void *a,const void *b,void *arg)
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
            res = pa->read_val - pb->read_val;
            break;
        case SORT_BY_WRITE:
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

void view_curses(struct xxxid_stats_arr *cs, struct xxxid_stats_arr *ps, struct act_stats *act)
{
    if (!stdscr)
    {
        initscr();
        keypad(stdscr, TRUE);
        nonl();
        cbreak();
        noecho();
        curs_set(FALSE);
        nodelay(stdscr, TRUE);
    }

    int diff_len = create_diff(cs, ps);
    struct xxxid_stats *s;

    static double total_read = 0, total_write = 0;
    double total_a_read, total_a_write;
    char *str_read, *str_write;
    char *str_a_read, *str_a_write;

    calc_total(cs, &total_read, &total_write);
    calc_a_total(act, &total_a_read, &total_a_write);

    humanize_val(&total_read, &str_read, config.f.accumulated);
    humanize_val(&total_write, &str_write, config.f.accumulated);
    humanize_val(&total_a_read, &str_a_read, 0);
    humanize_val(&total_a_write, &str_a_write, 0);

    mvprintw(0, 0, HEADER1_FORMAT,
             total_read,
             str_read,
             total_write,
             str_write
            );

    mvprintw(1, 0, HEADER2_FORMAT,
             total_a_read,
             str_a_read,
             total_a_write,
             str_a_write
            );

    int maxy = getmaxy(stdscr);
    int maxx = getmaxx(stdscr);

#define SORT_CHAR(x) ((sort_by == x) ? (sort_order == SORT_ASC ? '<' : '>') : ' ')
    attron(A_REVERSE);
    mvhline(2, 0, ' ', maxx);
    move(2, 0);

    int i;
    for (i = 0; i < SORT_BY_MAX; i++)
    {
        if (sort_by == i)
            attron(A_BOLD);
        printw(COLUMN_FORMAT(i), COLUMN_NAME(i), SORT_CHAR(i));
        if (sort_by == i)
            attroff(A_BOLD);
    }
    attroff(A_REVERSE);

    arr_sort(cs,my_sort_cb,(void *)(long)(sort_by * 2 + !!(sort_order == SORT_ASC)));

    int line = 3;
    int lastline = line;
    for (i = 0; cs->sor && i < diff_len; i++)
    {
        s = cs->sor[i];

        double read_val = s->read_val;
        double write_val = s->write_val;

        if (config.f.only && !read_val && !write_val)
            continue;

        char *read_str, *write_str;

        humanize_val(&read_val, &read_str, 1);
        humanize_val(&write_val, &write_str, 1);

        mvprintw(line, 0, "%5i  %4s  %-9.9s  %7.2f %-3.3s  %7.2f %-3.3s %5.2f %% %5.2f %%  %s\n",
                 s->tid,
                 str_ioprio(s->io_prio),
                 s->pw_name,
                 read_val,
                 read_str,
                 write_val,
                 write_str,
                 s->swapin_val,
                 s->blkio_val,
                 s->cmdline
                );
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

    refresh();
}

void view_curses_finish(void)
{
    endwin();
}

int curses_sleep(unsigned int seconds)
{
    fd_set fds;
    struct timeval tv;

    FD_ZERO(&fds);
    FD_SET(fileno(stdin), &fds);

    tv.tv_sec = seconds;
    tv.tv_usec = 0;

    int rv = select(1, &fds, NULL, NULL, &tv);

    if (rv)
    {
        switch (getch())
        {
        case 'q':
        case 'Q':
            return 1;
        case ' ':
        case 'r':
        case 'R':
            sort_order = (sort_order == SORT_ASC) ? SORT_DESC : SORT_ASC;
            return 0;
        case KEY_HOME:
            sort_by = 0;
            return 0;
        case KEY_END:
            sort_by = SORT_BY_MAX - 1;
            return 0;
        case KEY_RIGHT:
            if (++sort_by == SORT_BY_MAX)
                sort_by = SORT_BY_PID;
            return 0;
        case KEY_LEFT:
            if (--sort_by == -1)
                sort_by = SORT_BY_MAX - 1;
            return 0;
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
        }
    }

    return 0;
}
