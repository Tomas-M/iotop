#include "iotop.h"

#include <curses.h>
#include <math.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>

#define HEADER1_FORMAT "  Total DISK READ:   %7.2f %s |   Total DISK WRITE:   %7.2f %s"
#define HEADER2_FORMAT "Current DISK READ:   %7.2f %s | Current DISK WRITE:   %7.2f %s"

struct xxxid_stats *findpid(struct xxxid_stats *chain, int tid)
{
    struct xxxid_stats *s;

    for (s = chain; s; s = s->__next)
        if (s->tid == tid)
            return s;

    return NULL;
}

int chainlen(struct xxxid_stats *chain)
{
    int i = 0;

    while (chain)
    {
        i++;
        chain = chain->__next;
    }

    return i;
}

#define RRV(to, from) {\
    to = (to < from)\
        ? xxx - to + from\
        : to - from;\
}

struct xxxid_stats *create_diff(struct xxxid_stats *cs, struct xxxid_stats *ps, int *len)
{
    int diff_size = sizeof(struct xxxid_stats) * chainlen(cs);
    struct xxxid_stats *diff = malloc(diff_size);
    struct xxxid_stats *c;
    int n = 0;
    uint64_t xxx = ~0;

    memset(diff, 0, diff_size);

    for (c = cs; c; c = c->__next, n++)
    {
        struct xxxid_stats *p = findpid(ps, c->tid);

        if (!p)
        {
            // new process or task
            memcpy(&diff[n], c, sizeof(struct xxxid_stats));
            diff[n].read_bytes \
            = diff[n].write_bytes \
              = diff[n].swapin_delay_total \
                = diff[n].blkio_delay_total \
                  = 0;
            diff[n].__next = &diff[n + 1];
            continue;
        }

        memcpy(&diff[n], c, sizeof(struct xxxid_stats));

        // round robin value

        RRV(diff[n].read_bytes, p->read_bytes);
        RRV(diff[n].write_bytes, p->write_bytes);

        RRV(diff[n].swapin_delay_total, p->swapin_delay_total);
        RRV(diff[n].blkio_delay_total, p->blkio_delay_total);

        diff[n].blkio_val =
            (double) diff[n].blkio_delay_total / 10e9 / params.delay * 100;

        diff[n].swapin_val =
            (double) diff[n].swapin_delay_total / 10e9 / params.delay * 100;

        diff[n].read_val = (double) diff[n].read_bytes
                           / (config.f.accumulated ? 1 : params.delay);

        diff[n].write_val = (double) diff[n].write_bytes
                            / (config.f.accumulated ? 1 : params.delay);

        diff[n].__next = &diff[n + 1];
    }

    // No have previous data to calculate diff
    if (!n)
    {
        free(diff);
        return NULL;
    }

    diff[n - 1].__next = NULL;
    *len = n;

    return diff;
}

void calc_total(struct xxxid_stats *diff, double *read, double *write)
{
    struct xxxid_stats *s;
    *read = *write = 0;

    for (s = diff; s; s = s->__next)
    {
        *read += s->read_bytes;
        *write += s->write_bytes;
    }

    if (!config.f.accumulated)
    {
        *read /= params.delay;
        *write /= params.delay;
    }
}

void calc_a_total(struct act_stats *act, double *read, double *write)
{
    uint64_t xxx = ~0;

    *read = *write = 0;

    if (act->have_o)
    {
        uint64_t r = act->read_bytes;
        uint64_t w = act->write_bytes;

        RRV(r, act->read_bytes_o);
        RRV(w, act->write_bytes_o);
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

void view_batch(struct xxxid_stats *cs, struct xxxid_stats *ps, struct act_stats *act)
{
    int diff_len = 0;

    struct xxxid_stats *diff = create_diff(cs, ps, &diff_len);
    struct xxxid_stats *s;

    double total_read, total_write;
    double total_a_read, total_a_write;
    char *str_read, *str_write;
    char *str_a_read, *str_a_write;

    calc_total(diff, &total_read, &total_write);
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

    printf(HEADER2_FORMAT,
           total_a_read,
           str_a_read,
           total_a_write,
           str_a_write
          );

    if (config.f.timestamp)
    {
        time_t t = time(NULL);
        printf(" | %s", ctime(&t));
    }
    else
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

    for (s = diff; s; s = s->__next)
    {
        struct passwd *pwd = getpwuid(s->euid);

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
               pwd ? pwd->pw_name : "UNKNOWN",
               read_val,
               read_str,
               write_val,
               write_str,
               s->swapin_val,
               s->blkio_val,
               s->cmdline
              );
    }

    free(diff);
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

void sort_diff(struct xxxid_stats *d)
{
    int len = chainlen(d);
    int i;

    for (i = 0; i < len; i++)
    {
        int k;

        for (k = i; k < len; k++)
        {
            int found = 0;

#define CMP_FIELDS(field_name) (d[k].field_name > d[i].field_name)

            switch (sort_by)
            {
            case SORT_BY_PRIO:
                found = d[k].io_prio > d[i].io_prio;
                break;
            case SORT_BY_COMMAND:
                found = (strcmp(d[k].cmdline, d[i].cmdline) > 0);
                break;
            case SORT_BY_PID:
                found = CMP_FIELDS(tid);
                break;
            case SORT_BY_USER:
                found = CMP_FIELDS(euid);
                break;
            case SORT_BY_READ:
                found = CMP_FIELDS(read_val);
                break;
            case SORT_BY_WRITE:
                found = CMP_FIELDS(write_val);
                break;
            case SORT_BY_SWAPIN:
                found = CMP_FIELDS(swapin_val);
                break;
            case SORT_BY_IO:
                found = CMP_FIELDS(blkio_val);
                break;
            }

#undef CMP_FIELDS

            if (found)
            {
                static struct xxxid_stats tmp;

                memcpy(&tmp, &d[i], sizeof(struct xxxid_stats));
                memcpy(&d[i], &d[k], sizeof(struct xxxid_stats));
                memcpy(&d[k], &tmp, sizeof(struct xxxid_stats));
            }
        }
    }

    if (sort_order == SORT_ASC)
    {
        struct xxxid_stats *rev = malloc(sizeof(struct xxxid_stats) * len);
        for (i = 0; i < len; i++)
            memcpy(&rev[i], &d[len - i - 1], sizeof(struct xxxid_stats));
        memcpy(d, rev, sizeof(struct xxxid_stats) * len);
        free(rev);
    }

    for (i = 0; i < len; i++)
        d[i].__next = &d[i + 1];

    d[len - 1].__next = NULL;
}

void view_curses(struct xxxid_stats *cs, struct xxxid_stats *ps, struct act_stats *act)
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

    int diff_len = 0;

    struct xxxid_stats *diff = create_diff(cs, ps, &diff_len);
    struct xxxid_stats *s;

    double total_read, total_write;
    double total_a_read, total_a_write;
    char *str_read, *str_write;
    char *str_a_read, *str_a_write;

    calc_total(diff, &total_read, &total_write);
    calc_a_total(act, &total_a_read, &total_a_write);

    humanize_val(&total_read, &str_read, 0);
    humanize_val(&total_write, &str_write, 0);
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

    sort_diff(diff);

    int line = 3;
    int lastline = line;
    for (s = diff; s; s = s->__next)
    {
        struct passwd *pwd = getpwuid(s->euid);

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
                 pwd ? pwd->pw_name : "UNKNOWN",
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

    sort_diff(diff);
    free(diff);
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
