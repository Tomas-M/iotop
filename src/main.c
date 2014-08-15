#include <stdio.h>
#include <stdlib.h>

#include "iotop.h"

static char *progname = NULL;

void
_check_priv(void)
{
    if (geteuid()) {
        fprintf(stderr, "%s requires root privileges\n", progname);
        exit(-1);
    }
}

int
main(int argc, char *argv[])
{
    progname = argv[0];
    _check_priv();

    if (argc < 2) {
        fprintf(stderr, "USAGE: %s PID\n", progname);
        exit(-1);
    }

    int pid = atoi(argv[1]);
    struct xxxid_stats stats;

    nl_init();
    nl_xxxid_info(pid, 1, &stats);
    dump_xxxid_stats(&stats);
    nl_term();

    return 0;
}
