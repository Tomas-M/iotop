/* SPDX-License-Identifer: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020  Boian Bonev

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

#include "iotop.h"

#include <pwd.h>
#include <ctype.h>
#include <getopt.h>
#include <stdio.h>
#include <locale.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

static const char *progname=NULL;
int maxpidlen=5;

config_t config;
params_t params;

view_init v_init_cb=view_curses_init;
view_fini v_fini_cb=view_curses_fini;
view_loop v_loop_cb=view_curses_loop;

inline void init_params(void) {
	params.iter=-1;
	params.delay=1;
	params.pid=-1;
	params.user_id=-1;
}

static const char str_opt[]="boPaktqHcs123456789";

static inline void print_help(void) {
	printf(
		"Usage: %s [OPTIONS]\n\n"
		"DISK READ and DISK WRITE are the block I/O bandwidth used during the sampling\n"
		"period. SWAPIN and IO are the percentages of time the thread spent respectively\n"
		"while swapping in and waiting on I/O more generally. PRIO is the I/O priority\n"
		"at which the thread is running (set using the ionice command).\n\n"
		"Controls: left and right arrows to change the sorting column, r to invert the\n"
		"sorting order, o to toggle the --only option, p to toggle the --processes\n"
		"option, a to toggle the --accumulated option, i to change I/O priority, q to\n"
		"quit, any other key to force a refresh.\n\n"
		"Options:\n"
		"  -v, --version         show program's version number and exit\n"
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
		"  -c, --fullcmdline     show full command line\n"
		"  -1, --hide-pid        hide PID/TID column\n"
		"  -2, --hide-prio       hide PRIO column\n"
		"  -3, --hide-user       hide USER column\n"
		"  -4, --hide-read       hide DISK READ column\n"
		"  -5, --hide-write      hide DISK WRITE column\n"
		"  -6, --hide-swapin     hide SWAPIN column\n"
		"  -7, --hide-io         hide IO column\n"
		"  -8, --hide-graph      hide GRAPH column\n"
		"  -9, --hide-command    hide COMMAND column\n"
		"  -q, --quiet           suppress some lines of header (implies --batch)\n"
		"  -H, --no-help         suppress listing of shortcuts\n",
		progname
	);
}

static inline void parse_args(int argc,char *argv[]) {
	init_params();
	memset(&config,0,sizeof(config));
	config.f.sort_by=SORT_BY_GRAPH;
	config.f.sort_order=SORT_DESC;

	while (1) {
		static struct option long_options[]={
			{"version",no_argument,NULL,'v'},
			{"help",no_argument,NULL,'h'},
			{"batch",no_argument,NULL,'b'},
			{"only",no_argument,NULL,'o'},
			{"iter",required_argument,NULL,'n'},
			{"delay",required_argument,NULL,'d'},
			{"pid",required_argument,NULL,'p'},
			{"user",required_argument,NULL,'u'},
			{"processes",no_argument,NULL,'P'},
			{"accumulated",no_argument,NULL,'a'},
			{"kilobytes",no_argument,NULL,'k'},
			{"timestamp",no_argument,NULL,'t'},
			{"quiet",no_argument,NULL,'q'},
			{"no-help",no_argument,NULL,'H'},
			{"fullcmdline",no_argument,NULL,'c'},
			{"iohistory",no_argument,NULL,'s'},
			{"hide-pid",no_argument,NULL,'1'},
			{"hide-prio",no_argument,NULL,'2'},
			{"hide-user",no_argument,NULL,'3'},
			{"hide-read",no_argument,NULL,'4'},
			{"hide-write",no_argument,NULL,'5'},
			{"hide-swapin",no_argument,NULL,'6'},
			{"hide-io",no_argument,NULL,'7'},
			{"hide-graph",no_argument,NULL,'8'},
			{"hide-command",no_argument,NULL,'9'},
			{NULL,0,NULL,0}
		};

		int c=getopt_long(argc,argv,"vhbon:d:p:u:PaktqcsH123456789",long_options,NULL);

		if (c==-1)
			break;

		switch (c) {
			case 'v':
				printf("%s %s\n",argv[0],VERSION);
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
			case 'H':
			case 'c':
			case 's':
			case '1' ... '9':
				config.opts[(unsigned int)(strchr(str_opt,c)-str_opt)]=1;
				break;
			case 'n':
				params.iter=atoi(optarg);
				break;
			case 'd':
				params.delay=atoi(optarg);
				break;
			case 'p':
				params.pid=atoi(optarg);
				break;
			case 'u':
				if (isdigit(optarg[0]))
					params.user_id=atoi(optarg);
				else {
					struct passwd *pwd=getpwnam(optarg);

					if (!pwd) {
						fprintf(stderr,"%s: user %s not found\n",progname,optarg);
						exit(EXIT_FAILURE);
					}
					params.user_id=pwd->pw_uid;
				}
				break;
			default:
				fprintf(stderr,"%s: unknown option\n",progname);
				exit(EXIT_FAILURE);
		}
	}
}

inline void sig_handler(int signo) {
	if (signo==SIGINT) {
		v_fini_cb();
		nl_fini();
		exit(EXIT_SUCCESS);
	}
}

int main(int argc,char *argv[]) {
	progname=argv[0];

	parse_args(argc,argv);
	if (system_checks())
		return EXIT_FAILURE;

	setlocale(LC_ALL,"");
	nl_init();

	if (signal(SIGINT,sig_handler)==SIG_ERR)
		perror("signal");

	if (config.f.timestamp||config.f.quiet)
		config.f.batch_mode=1;

	if (config.f.batch_mode) {
		v_init_cb=view_batch_init;
		v_fini_cb=view_batch_fini;
		v_loop_cb=view_batch_loop;
	}

	v_init_cb();
	v_loop_cb();
	v_fini_cb();
	nl_fini();

	return 0;
}
