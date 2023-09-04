/* SPDX-License-Identifier: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020-2023  Boian Bonev

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

#include "iotop.h"

#include <pwd.h>
#include <stdlib.h>
#include <string.h>



inline void free_stats(struct xxxid_stats *s) {
	if (s->cmdline1)
		free(s->cmdline1);
	if (s->cmdline2)
		free(s->cmdline2);
	if (s->pw_name)
		free(s->pw_name);
	arr_free_noitem(s->threads);

	free(s);
}

inline struct xxxid_stats *make_stats(pid_t tid,pid_t pid) {
	struct xxxid_stats *s=calloc(1,sizeof *s);
	static const char unknown[]="<unknown>";
	struct passwd *pwd;
	char *cmdline1;
	char *cmdline2;
	int prio;

	if (!s)
		return NULL;

	if ( SWITCH(nl_xxxid_info)(tid,pid,s) )
		s->error_x=1;


	prio=get_ioprio(tid);
	if (prio==-1) {
		s->error_i=1;
		s->io_prio=0;
	} else
		s->io_prio=prio;

	cmdline1=read_cmdline(tid,1);
	cmdline2=read_cmdline(tid,0);

	s->cmdline1=cmdline1?cmdline1:strdup(unknown);
	s->cmdline2=cmdline2?cmdline2:strdup(unknown);
	pwd=getpwuid(s->euid);
	s->pw_name=strdup(pwd&&pwd->pw_name?pwd->pw_name:unknown);

	if ((s->error_x||s->error_i||!cmdline1||!cmdline2)&&!is_a_process(tid)) { // process exited in the meantime
		free_stats(s);
		return NULL;
	}
	return s;
}

static void pid_cb(pid_t pid,pid_t tid,struct xxxid_stats_arr *a,filter_callback filter) {
	struct xxxid_stats *s=make_stats(tid,pid);

	if (s) {
		if (filter&&filter(s))
			free_stats(s);
		else {
			if (s->pid!=s->tid) { // maintain a thread list for each process
				struct xxxid_stats *p=arr_find(a,s->pid); // main process' tid=thread's pid

				if (p) {
					// aggregate thread data into the main process
					if (!p->threads)
						p->threads=arr_alloc();
					if (p->threads) {
						arr_add(p->threads,s);
						p->swapin_delay_total+=s->swapin_delay_total;
						p->blkio_delay_total+=s->blkio_delay_total;
						p->read_bytes+=s->read_bytes;
						p->write_bytes+=s->write_bytes;
					}
				}
			}
			arr_add(a,s);
		}
	}
}

inline struct xxxid_stats_arr *fetch_data(filter_callback filter) {
	struct xxxid_stats_arr *a=arr_alloc();

	if (!a)
		return NULL;

	pidgen_cb((pg_cb)pid_cb,a,filter);
	return a;
}

