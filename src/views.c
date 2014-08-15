#include <stdio.h>
#include <stdlib.h>

#include "iotop.h"

void view_batch(struct xxxid_stats *cs, struct xxxid_stats *ps)
{
    struct xxxid_stats *s;

    for (s = cs; s; s = s->__next)
        dump_xxxid_stats(s);
}

void view_curses(struct xxxid_stats *cs, struct xxxid_stats *ps)
{
    fprintf(stderr, "###### VIEW CURSES IS NOT IMPLEMENTED YET ######\n");
    view_batch(cs, ps);
}
