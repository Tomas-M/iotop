#include "iotop.h"

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static char *progname = NULL;

config_t config;
params_t params;

void
init_params(void)
{
    params.iter = -1;
    params.delay = 1;
    params.pid = -1;
    params.user_id = -1;
}

static char str_opt[] = "boPaktq";

void
check_priv(void)
{
    if (geteuid() == 0)
        return;

    errno = EACCES;
    perror(progname);

    exit(EXIT_FAILURE);
}

void
print_help(void)
{
    printf(
        "Usage: %s [OPTIONS]\n\n"
        "DISK READ and DISK WRITE are the block I/O bandwidth used during the sampling\n"
        "period. SWAPIN and IO are the percentages of time the thread spent respectively\n"
        "while swapping in and waiting on I/O more generally. PRIO is the I/O priority at\n"
        "which the thread is running (set using the ionice command).\n\n"
        "Controls: left and right arrows to change the sorting column, r to invert the\n"
        "sorting order, o to toggle the --only option, p to toggle the --processes\n"
        "option, a to toggle the --accumulated option, q to quit, any other key to force\n"
        "a refresh.\n\n"
        "Options:\n"
        "  --version             show program's version number and exit\n"
        "  -h, --help            show this help message and exit\n"
        "  -o, --only            only show processes or threads actually doing I/O\n"
        "  -b, --batch           non-interactive mode\n"
        "  -n NUM, --iter=NUM    number of iterations before ending [infinite]\n"
        "  -d SEC, --delay=SEC   delay between iterations [1 second]\n"
        "  -p PID, --pid=PID     processes/threads to monitor [all]\n"
        "  -u USER, --user=USER  users to monitor [all]\n"
        "  -P, --processes       only show processes, not all threads\n"
        "  -a, --accumulated     show accumulated I/O instead of bandwidth\n"
        "  -k, --kilobytes       use kilobytes instead of a human friendly unit\n"
        "  -t, --time            add a timestamp on each line (implies --batch)\n"
        "  -q, --quiet           suppress header line output (implies --batch)\n",
        progname
    );
}

void
parse_args(int argc, char *argv[])
{
    init_params();
    memset(&config, 0, sizeof(config));

    while (1)
    {
        static struct option long_options[] =
        {
            {"version",     no_argument, NULL, 'v'},
            {"help",        no_argument, NULL, 'h'},
            {"batch",       no_argument, NULL, 'b'},
            {"only",        no_argument, NULL, 'o'},
            {"iter",        required_argument, NULL, 'n'},
            {"delay",       required_argument, NULL, 'd'},
            {"pid",         required_argument, NULL, 'p'},
            {"user",        required_argument, NULL, 'u'},
            {"processes",   no_argument, NULL, 'P'},
            {"accumulated", no_argument, NULL, 'a'},
            {"kilobytes",   no_argument, NULL, 'k'},
            {"timestamp",   no_argument, NULL, 't'},
            {"quite",       no_argument, NULL, 'q'},
            {NULL, 0, NULL, 0}
        };

        int c = getopt_long(argc, argv, "vhbon:d:p:u:Paktq",
                            long_options, NULL);

        if (c == -1)
            break;

        switch (c)
        {
        case 'v':
            printf("%s %s\n", argv[0], VERSION);
            exit(EXIT_SUCCESS);
        case 'h':
            print_help();
            exit(EXIT_SUCCESS);
        case 'o':
        case 'b':
        case 'P':
        case 'a':
        case 'k':
        case 't':
        case 'q':
            config.opts[(unsigned int) (strchr(str_opt, c) - str_opt)] = 1;
            break;
        case 'n':
            params.iter = atoi(optarg);
            break;
        case 'd':
            params.delay = atoi(optarg);
            break;
        case 'p':
            params.pid = atoi(optarg);
            break;
        case 'u':
            if (isdigit(optarg[0]))
                params.user_id = atoi(optarg);
            else
            {
                struct passwd *pwd = getpwnam(optarg);
                if (!pwd)
                {
                    fprintf(stderr, "%s: user %s not found\n",
                            progname, optarg);
                    exit(EXIT_FAILURE);
                }
                params.user_id = pwd->pw_uid;
            }
            break;
        default:
            fprintf(stderr, "%s: unknown option\n", progname);
            exit(EXIT_FAILURE);
        }
    }
}

int
filter1(struct xxxid_stats *s)
{
    if ((params.user_id != -1) && (s->euid != params.user_id))
        return 1;

    if ((params.pid != -1) && (s->tid != params.pid))
        return 1;

    return 0;
}

void
sig_handler(int signo)
{
    if (signo == SIGINT)
    {
        nl_term();
        if (!config.f.batch_mode)
            view_curses_finish();

        exit(EXIT_SUCCESS);
    }
}

int
main(int argc, char *argv[])
{
    progname = argv[0];

    parse_args(argc, argv);
    check_priv();

    nl_init();

    if (signal(SIGINT, sig_handler) == SIG_ERR)
        perror("signal");

    struct xxxid_stats *ps = NULL;
    struct xxxid_stats *cs = NULL;

    if (config.f.timestamp || config.f.quite)
        config.f.batch_mode = 1;

    view_callback view = view_batch;
    how_to_sleep do_sleep = (how_to_sleep) sleep;

    if (!config.f.batch_mode)
    {
        view = view_curses;
        do_sleep = curses_sleep;
    }

    do
    {
        cs = fetch_data(config.f.processes, filter1);
        view(cs, ps);

        if (ps)
            free_stats_chain(ps);

        ps = cs;
        if ((params.iter > -1) && ((--params.iter) == 0))
            break;
    }
    while (!do_sleep(params.delay));

    free_stats_chain(cs);
    sig_handler(SIGINT);

    return 0;
}
