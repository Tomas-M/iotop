/* SPDX-License-Identifier: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020-2023  Boian Bonev

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

#define OPT_SI 0x100
#define OPT_THR 0x101
#define OPT_ASCII 0x102
#define OPT_NO_ONLY 0x103
#define OPT_THREADS 0x104
#define OPT_NO_ACCUMULATED 0x105
#define OPT_NO_KILOBYTES 0x106
#define OPT_NO_FULLCMDLINE 0x107
#define OPT_SHOW_PID 0x108
#define OPT_SHOW_PRIO 0x109
#define OPT_SHOW_USER 0x10a
#define OPT_SHOW_READ 0x10b
#define OPT_SHOW_WRITE 0x10c
#define OPT_SHOW_SWAPIN 0x10d
#define OPT_SHOW_IO 0x10e
#define OPT_SHOW_GRAPH 0x10f
#define OPT_SHOW_CMD 0x110
#define OPT_NO_DEADX 0x111
#define OPT_SHOW_EXITED 0x112
#define OPT_NO_REVERSE 0x113
#define OPT_NO_SI 0x114
#define OPT_UNICODE 0x115
#define OPT_COLOR 0x116

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

static const char str_opt[]="boPaktqc123456789xelR";

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
		"  -v, --version          show program's version number and exit\n"
		"  -h, --help             show this help message and exit\n"
		"  -H, --help-type=TYPE   set type of interactive help (none, win or inline)\n"
		"  -o, --only             only show processes or threads actually doing I/O\n"
		"      --no-only          show all processes or threads\n"
		"  -b, --batch            non-interactive mode\n"
		"  -n NUM, --iter=NUM     number of iterations before ending [infinite]\n"
		"  -d SEC, --delay=SEC    delay between iterations [1 second]\n"
		"  -p PID, --pid=PID      processes/threads to monitor [all]\n"
		"  -u USER, --user=USER   users to monitor [all]\n"
		"  -P, --processes        only show processes, not all threads\n"
		"      --threads          show all threads\n"
		"  -a, --accumulated      show accumulated I/O instead of bandwidth\n"
		"      --no-accumulated   show bandwidth\n"
		"  -k, --kilobytes        use kilobytes instead of a human friendly unit\n"
		"      --no-kilobytes     use human friendly unit\n"
		"  -t, --time             add a timestamp on each line (implies --batch)\n"
		"  -c, --fullcmdline      show full command line\n"
		"      --no-fullcmdline   show program names only\n"
		"  -1, --hide-pid         hide PID/TID column\n"
		"      --show-pid         show PID/TID column\n"
		"  -2, --hide-prio        hide PRIO column\n"
		"      --show-prio        show PRIO column\n"
		"  -3, --hide-user        hide USER column\n"
		"      --show-user        show USER column\n"
		"  -4, --hide-read        hide DISK READ column\n"
		"      --show-read        show DISK READ column\n"
		"  -5, --hide-write       hide DISK WRITE column\n"
		"      --show-write       show DISK WRITE column\n"
		"  -6, --hide-swapin      hide SWAPIN column\n"
		"      --show-swapin      show SWAPIN column\n"
		"  -7, --hide-io          hide IO column\n"
		"      --show-io          show IO column\n"
		"  -8, --hide-graph       hide GRAPH column\n"
		"      --show-graph       show GRAPH column\n"
		"  -9, --hide-command     hide COMMAND column\n"
		"      --show-command     show COMMAND column\n"
		"  -g TYPE, --grtype=TYPE set graph data source (io, r, w, rw and sw)\n"
		"  -R, --reverse-graph    reverse GRAPH column direction\n"
		"      --no-reverse-graph do not reverse GRAPH column direction\n"
		"  -q, --quiet            suppress some lines of header (implies --batch)\n"
		"  -x, --dead-x           show exited processes/threads with letter x\n"
		"      --no-dead-x        show exited processes/threads with background\n"
		"  -e, --hide-exited      hide exited processes\n"
		"      --show-exited      show exited processes\n"
		"  -l, --no-color         do not colorize values\n"
		"      --color            colorize values\n"
		"      --si               use SI units of 1000 when printing values\n"
		"      --no-si            use non-SI units of 1024 when printing values\n"
		"      --threshold=1..10  threshold to switch to next unit\n"
		"      --ascii            disable using Unicode\n"
		"      --unicode          use Unicode drawing chars\n"
		"  -W, --write            write preceding options to the config and exit\n",
		progname
	);
}

static inline void parse_args(int clac,char **clav) {
	char *no_color=getenv("NO_COLOR");
	int v;
	int i;

	init_params();
	memset(&config,0,sizeof(config));
	config.f.sort_by=SORT_BY_GRAPH;
	config.f.sort_order=SORT_DESC;
	config.f.base=1024; // use non-SI units by default
	config.f.threshold=2; // default threshold is 2*base
	config.f.unicode=1; // default is unicode

	// implement https://no-color.org/ proposal
	if (no_color&&*no_color)
		config.f.nocolor=1;

	for (i=0;i<2;i++) {
		char **argv;
		int argc;

		if (i==0) { // process config file
			if (config_file_load(&argc,&argv))
				continue; // did not load, ignore
			optind=1;
		} else { // process command line options
			config_file_free();
			argc=clac;
			argv=clav;
			optind=1;
		}
		for (;;) {
			static struct option long_options[]={
				{"version",no_argument,NULL,'v'},
				{"help",no_argument,NULL,'h'},
				{"help-type",required_argument,NULL,'H'},
				{"batch",no_argument,NULL,'b'},
				{"only",no_argument,NULL,'o'},
				{"no-only",no_argument,NULL,OPT_NO_ONLY},
				{"iter",required_argument,NULL,'n'},
				{"delay",required_argument,NULL,'d'},
				{"pid",required_argument,NULL,'p'},
				{"user",required_argument,NULL,'u'},
				{"processes",no_argument,NULL,'P'},
				{"threads",no_argument,NULL,OPT_THREADS},
				{"accumulated",no_argument,NULL,'a'},
				{"no-accumulated",no_argument,NULL,OPT_NO_ACCUMULATED},
				{"kilobytes",no_argument,NULL,'k'},
				{"no-kilobytes",no_argument,NULL,OPT_NO_KILOBYTES},
				{"timestamp",no_argument,NULL,'t'},
				{"quiet",no_argument,NULL,'q'},
				{"fullcmdline",no_argument,NULL,'c'},
				{"no-fullcmdline",no_argument,NULL,OPT_NO_FULLCMDLINE},
				{"hide-pid",no_argument,NULL,'1'},
				{"show-pid",no_argument,NULL,OPT_SHOW_PID},
				{"hide-prio",no_argument,NULL,'2'},
				{"show-prio",no_argument,NULL,OPT_SHOW_PRIO},
				{"hide-user",no_argument,NULL,'3'},
				{"show-user",no_argument,NULL,OPT_SHOW_USER},
				{"hide-read",no_argument,NULL,'4'},
				{"show-read",no_argument,NULL,OPT_SHOW_READ},
				{"hide-write",no_argument,NULL,'5'},
				{"show-write",no_argument,NULL,OPT_SHOW_WRITE},
				{"hide-swapin",no_argument,NULL,'6'},
				{"show-swapin",no_argument,NULL,OPT_SHOW_SWAPIN},
				{"hide-io",no_argument,NULL,'7'},
				{"show-io",no_argument,NULL,OPT_SHOW_IO},
				{"hide-graph",no_argument,NULL,'8'},
				{"show-graph",no_argument,NULL,OPT_SHOW_GRAPH},
				{"hide-command",no_argument,NULL,'9'},
				{"show-command",no_argument,NULL,OPT_SHOW_CMD},
				{"dead-x",no_argument,NULL,'x'},
				{"no-dead-x",no_argument,NULL,OPT_NO_DEADX},
				{"hide-exited",no_argument,NULL,'e'},
				{"show-exited",no_argument,NULL,OPT_SHOW_EXITED},
				{"no-color",no_argument,NULL,'l'},
				{"color",no_argument,NULL,OPT_COLOR},
				{"reverse-graph",no_argument,NULL,'R'},
				{"no-reverse-graph",no_argument,NULL,OPT_NO_REVERSE},
				{"grtype",required_argument,NULL,'g'},
				{"si",no_argument,NULL,OPT_SI},
				{"no-si",no_argument,NULL,OPT_NO_SI},
				{"threshold",required_argument,NULL,OPT_THR},
				{"ascii",no_argument,NULL,OPT_ASCII},
				{"unicode",no_argument,NULL,OPT_UNICODE},
				{"write",no_argument,NULL,'W'},
				{NULL,0,NULL,0}
			};

			int c=getopt_long(argc,argv,"vhbon:d:p:u:Paktqc123456789xelRg:H:W",long_options,NULL);

			if (c==-1) {
				if (optind<argc) {
					int i;

					for (i=optind;i<argc;i++)
						fprintf(stderr,"%s: unknown argument: %s\n",progname,argv[i]);
					exit(EXIT_FAILURE);
				}
				break;
			}

			switch (c) {
				case 'v':
					printf("%s %s\n",argv[0],VERSION);
					exit(EXIT_SUCCESS);
				case 'h':
					print_help();
					exit(EXIT_SUCCESS);
				case 'W':
					config_file_save();
					exit(EXIT_SUCCESS);
				case 'H': // below values are not partial prefixes of each other, do a relaxed match
					if (!strncmp(optarg,"none",strlen(optarg)))
						config.f.helptype=0;
					else if (!strncmp(optarg,"win",strlen(optarg)))
						config.f.helptype=1;
					else if (!strncmp(optarg,"inline",strlen(optarg)))
						config.f.helptype=2;
					else {
						fprintf(stderr,"%s: invalid value %s for interactive help type\n",progname,optarg);
						exit(EXIT_FAILURE);
					}
					break;
				case 'o':
				case 'b':
				case 'P':
				case 'a':
				case 'k':
				case 't':
				case 'q':
				case 'c':
				case '1' ... '9':
				case 'x':
				case 'e':
				case 'l':
				case 'R':
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
				case 'g': // below values are not partial prefixes of each other, do a relaxed match
					// except r is a prefix of rw, but r is matched first
					// do an exact match for r and w - there is no point in relaxed match for single letter values
					if (!strncmp(optarg,"io",strlen(optarg)))
						config.f.grtype=E_GR_IO;
					else if (!strcmp(optarg,"r"))
						config.f.grtype=E_GR_R;
					else if (!strcmp(optarg,"w"))
						config.f.grtype=E_GR_W;
					else if (!strcmp(optarg,"rw"))
						config.f.grtype=E_GR_RW;
					else if (!strncmp(optarg,"sw",strlen(optarg)))
						config.f.grtype=E_GR_SW;
					else {
						fprintf(stderr,"%s: invalid value %s for graph type\n",progname,optarg);
						exit(EXIT_FAILURE);
					}
					break;
				case 'u':
					if (optarg[0]=='+') // always interpret as numeric uid
						params.user_id=atoi(optarg+1);
					else {
						struct passwd *pwd=getpwnam(optarg);

						if (!pwd) {
							if (isdigit(optarg[0])) { // fallback to numeric uid
								params.user_id=atoi(optarg);
								break;
							}
							fprintf(stderr,"%s: user %s not found\n",progname,optarg);
							exit(EXIT_FAILURE);
						}
						params.user_id=pwd->pw_uid;
					}
					break;
				case OPT_SI:
					config.f.base=1000;
					break;
				case OPT_THR:
					v=atoi(optarg);
					if (v<1||v>10) {
						fprintf(stderr,"%s: threshold %s is not between 1 and 10\n",progname,optarg);
						exit(EXIT_FAILURE);
					}
					config.f.threshold=v;
					break;
				case OPT_ASCII:
					config.f.unicode=0;
					break;
				case OPT_NO_ONLY:
					config.f.only=0;
					break;
				case OPT_THREADS:
					config.f.processes=0;
					break;
				case OPT_NO_ACCUMULATED:
					config.f.accumulated=0;
					break;
				case OPT_NO_KILOBYTES:
					config.f.kilobytes=0;
					break;
				case OPT_NO_FULLCMDLINE:
					config.f.fullcmdline=0;
					break;
				case OPT_SHOW_PID:
					config.f.hidepid=0;
					break;
				case OPT_SHOW_PRIO:
					config.f.hideprio=0;
					break;
				case OPT_SHOW_USER:
					config.f.hideuser=0;
					break;
				case OPT_SHOW_READ:
					config.f.hideread=0;
					break;
				case OPT_SHOW_WRITE:
					config.f.hidewrite=0;
					break;
				case OPT_SHOW_SWAPIN:
					config.f.hideswapin=0;
					break;
				case OPT_SHOW_IO:
					config.f.hideio=0;
					break;
				case OPT_SHOW_GRAPH:
					config.f.hidegraph=0;
					break;
				case OPT_SHOW_CMD:
					config.f.hidecmd=0;
					break;
				case OPT_NO_DEADX:
					config.f.deadx=0;
					break;
				case OPT_SHOW_EXITED:
					config.f.hideexited=0;
					break;
				case OPT_NO_REVERSE:
					config.f.reverse_graph=0;
					break;
				case OPT_NO_SI:
					config.f.base=1024;
					break;
				case OPT_UNICODE:
					config.f.unicode=1;
					break;
				case OPT_COLOR:
					config.f.nocolor=0;
					break;
				default:
					exit(EXIT_FAILURE);
			}
		}
	}
}

inline void sig_handler(int signo) {
	switch (signo) {
		default:
			break;
		case SIGINT:
		case SIGHUP:
		case SIGQUIT:
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
	if (signal(SIGHUP,sig_handler)==SIG_ERR)
		perror("signal");
	if (signal(SIGQUIT,sig_handler)==SIG_ERR)
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
