#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "iotop.h"

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
    __u64 xxx = ~0;

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

#undef RRV

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

void view_batch(struct xxxid_stats *cs, struct xxxid_stats *ps)
{
    int diff_len = 0;
    struct xxxid_stats *diff = create_diff(cs, ps, &diff_len);
    struct xxxid_stats *s;

    for (s = diff; s; s = s->__next)
        dump_xxxid_stats(s);

    free(diff);
}

void view_curses(struct xxxid_stats *cs, struct xxxid_stats *ps)
{
    fprintf(stderr, "###### VIEW CURSES IS NOT IMPLEMENTED YET ######\n");
    view_batch(cs, ps);
}
