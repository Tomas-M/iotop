#include "iotop.h"

#include <curses.h>
#include <math.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>

#define HEADER_FORMAT "Total DISK READ: %7.2f %s | Total DISK WRITE: %7.2f %s"

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

    while (chain) {
        i++;
        chain = chain->__next;
    }

    return i;
}

struct xxxid_stats *create_diff(struct xxxid_stats *cs, struct xxxid_stats *ps, int *len)
{
    int diff_size = sizeof(struct xxxid_stats) * chainlen(cs);
    struct xxxid_stats *diff = malloc(diff_size);
    struct xxxid_stats *c;
    int n = 0;
    uint64_t xxx = ~0;

    memset(diff, 0, diff_size);

    for (c = cs; c; c = c->__next, n++) {
        struct xxxid_stats *p = findpid(ps, c->tid);

        if (!p) {
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

#define RRV(to, from) {\
    to = (to < from)\
        ? xxx - to + from\
        : to - from;\
}
        RRV(diff[n].read_bytes, p->read_bytes);
        RRV(diff[n].write_bytes, p->write_bytes);

        RRV(diff[n].swapin_delay_total, p->swapin_delay_total);
        RRV(diff[n].blkio_delay_total, p->blkio_delay_total);

        static double pow_ten = 0;
        if (!pow_ten)
            pow_ten = pow(10, 9);

#undef RRV

        diff[n].blkio_val =
            (double) diff[n].blkio_delay_total / pow_ten / params.delay * 100;

        diff[n].swapin_val =
            (double) diff[n].swapin_delay_total / pow_ten / params.delay * 100;

        diff[n].read_val = (double) diff[n].read_bytes
            / (config.f.accumulated ? 1 : params.delay);

        diff[n].write_val = (double) diff[n].write_bytes
            / (config.f.accumulated ? 1 : params.delay);

        diff[n].__next = &diff[n + 1];
    }

    // No have previous data to calculate diff
    if (!n) {
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

    for (s = diff; s; s = s->__next) {
        *read += s->read_bytes;
        *write += s->write_bytes;
    }

    if (!config.f.accumulated) {
        *read /= params.delay;
        *write /= params.delay;
    }
}

void humanize_val(double *value, char **str)
{
    static char *prefix_acc[] = {"B", "K", "M", "G", "T"};
    static char *prefix[] = {"B/s", "K/s", "M/s", "G/s", "T/s"};

    int p = 0;
    while (*value > 10000 && p < 5) {
        *value /= 1000.0;
        p++;
    }

    *str = config.f.accumulated ? prefix_acc[p] : prefix[p];
}

void view_batch(struct xxxid_stats *cs, struct xxxid_stats *ps)
{
    int diff_len = 0;

    struct xxxid_stats *diff = create_diff(cs, ps, &diff_len);
    struct xxxid_stats *s;

    double total_read, total_write;
    char *str_read, *str_write;

    calc_total(diff, &total_read, &total_write);

    humanize_val(&total_read, &str_read);
    humanize_val(&total_write, &str_write);

    printf(HEADER_FORMAT,
        total_read,
        str_read,
        total_write,
        str_write
    );

    if (config.f.timestamp) {
        time_t t = time(NULL);
        printf(" | %s", ctime(&t));
    } else
        printf("\n");

    if (!config.f.quite)
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

    for (s = diff; s; s = s->__next) {
        struct passwd *pwd = getpwuid(s->euid);

        double read_val = s->read_val;
        double write_val = s->write_val;

        if (config.f.only && (!read_val || !write_val)) {
            continue;
        }

        char *read_str, *write_str;

        if (config.f.kilobytes) {
            read_val /= 1000;
            write_val /= 1000;
            read_str = config.f.accumulated ? "K" : "K/s";
            write_str = config.f.accumulated ? "K" : "K/s";
        } else {
            humanize_val(&read_val, &read_str);
            humanize_val(&write_val, &write_str);
        }

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

enum {
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

enum {
    SORT_DESC,
    SORT_ASC
};

static int sort_by = SORT_BY_IO;
static int sort_order = SORT_DESC;

void sort_diff(struct xxxid_stats *d)
{
    int len = chainlen(d);
    int i;

    for (i = 0; i < len; i++) {
        int k;

        for (k = i; k < len; k++) {
            int found = 0;

#define CMP_FIELDS(field_name) (d[k].field_name > d[i].field_name)

            switch (sort_by) {
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

            if (found) {
                static struct xxxid_stats tmp;

                memcpy(&tmp, &d[i], sizeof(struct xxxid_stats));
                memcpy(&d[i], &d[k], sizeof(struct xxxid_stats));
                memcpy(&d[k], &tmp, sizeof(struct xxxid_stats));
            }
        }
    }

    if (sort_order == SORT_ASC) {
        struct xxxid_stats *rev = malloc(sizeof(struct xxxid_stats) * len);
        for (i = 0; i < len; i++) {
            memcpy(&rev[i], &d[len - i - 1], sizeof(struct xxxid_stats));
        }
        memcpy(d, rev, sizeof(struct xxxid_stats) * len);
        free(rev);
    }

    for (i = 0; i < len; i++) {
        d[i].__next = &d[i + 1];
    }

    d[len - 1].__next = NULL;
}

void view_curses(struct xxxid_stats *cs, struct xxxid_stats *ps)
{
    static int initilized = 0;

    if (!initilized) {
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
    char *str_read, *str_write;

    calc_total(diff, &total_read, &total_write);

    humanize_val(&total_read, &str_read);
    humanize_val(&total_write, &str_write);

    mvprintw(0, 0, HEADER_FORMAT,
        total_read,
        str_read,
        total_write,
        str_write
    );

#define SORT_CHAR(x) ((sort_by == x) ? (sort_order == SORT_ASC ? '<' : '>') : ' ')
    attron(A_REVERSE);
    mvhline(1, 0, ' ', 1000);
    mvprintw(1, 0, "%5s%c %4s%c %8s%c %11s%c %11s%c %6s%c %6s%c %s%c",
        config.f.processes ? "PID" : "TID", SORT_CHAR(SORT_BY_PID),
        "PRIO", SORT_CHAR(SORT_BY_PRIO),
        "USER", SORT_CHAR(SORT_BY_USER),
        "DISK READ", SORT_CHAR(SORT_BY_READ),
        "DISK WRITE", SORT_CHAR(SORT_BY_WRITE),
        "SWAPIN", SORT_CHAR(SORT_BY_SWAPIN),
        "IO", SORT_CHAR(SORT_BY_IO),
        "COMMAND", SORT_CHAR(SORT_BY_COMMAND)
    );
    attroff(A_REVERSE);

    sort_diff(diff);

    int line = 2;
    for (s = diff; s; s = s->__next, line++) {
        struct passwd *pwd = getpwuid(s->euid);

        double read_val = s->read_val;
        double write_val = s->write_val;

        if (config.f.only && (!read_val || !write_val)) {
            continue;
        }

        char *read_str, *write_str;

        if (config.f.kilobytes) {
            read_val /= 1000;
            write_val /= 1000;
            read_str = config.f.accumulated ? "K" : "K/s";
            write_str = config.f.accumulated ? "K" : "K/s";
        } else {
            humanize_val(&read_val, &read_str);
            humanize_val(&write_val, &write_str);
        }

        mvprintw(line, 0, "%5i  %4s %-9.9s  %7.2f %-3.3s  %7.2f %-3.3s  %5.2f %%  %5.2f %%  %s\n",
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

    if (rv) {
        switch (getch()) {
            case 'q':
                return 1;
            case ' ':
                sort_order = (sort_order == SORT_ASC) ? SORT_DESC : SORT_ASC;
                return 0;
            case KEY_RIGHT:
                if (++sort_by == SORT_BY_MAX) {
                    sort_by = SORT_BY_PID;
                }
                return 0;
            case KEY_LEFT:
                if (--sort_by == -1) {
                    sort_by = SORT_BY_MAX - 1;
                }
                return 0;
        }
    }

    return 0;
}
