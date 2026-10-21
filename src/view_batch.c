/* SPDX-License-Identifier: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020-2026  Boian Bonev

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

#include "iotop.h"

#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// the batch view never shows exited tasks, threads only when processes are not aggregated, and the filtered ones
static int batch_keep(const struct xxxid_stats *s) {
	return !s->exited&&(!config.f.processes||s->pid==s->tid)&&!filter1(s);
}

static inline void view_batch(struct xxxid_stats_arr *cs,struct xxxid_stats_arr *ps,struct act_stats *act) {
	double time_s=timediff_in_s(act->ts_o,act->ts_c);
	uint64_t tot_r,tot_w,tot_a_r,tot_a_w; // bytes of the cycle, or accumulated
	double total_a_read,total_a_write;
	char str_a_read[4],str_a_write[4];
	double total_read,total_write;
	char str_read[4],str_write[4];
	static int firsthdr=1;
	char tstr[16]=""; // time prefix of each line with --time
	struct tm *tm;
	time_t t;
	int i;

	create_diff(cs,ps,timediff_in_ms(act->ts_o,act->ts_c),act->ts_c,0); // the batch view does not show exited tasks
	calc_total(cs,&tot_r,&tot_w);
	calc_a_total(act,&tot_a_r,&tot_a_w);
	total_read=config.f.accumulated?(double)tot_r:(double)tot_r/time_s;
	total_write=config.f.accumulated?(double)tot_w:(double)tot_w/time_s;
	total_a_read=(double)tot_a_r/time_s;
	total_a_write=(double)tot_a_w/time_s;

	humanize_val(&total_read,str_read,1);
	humanize_val(&total_write,str_write,1);
	humanize_val(&total_a_read,str_a_read,0);
	humanize_val(&total_a_write,str_a_write,0);

	if (config.f.timestamp) { // the same time for all lines of the iteration
		t=time(NULL);
		tm=localtime(&t);
		if (tm)
			strftime(tstr,sizeof tstr,"%H:%M:%S ",tm);
	}

	if (config.f.quiet<3) {
		printf("%s",tstr);
		printf(HEADER1_FORMAT,total_read,str_read,"",total_write,str_write,"");
		printf("\n");
		printf("%s",tstr);
		printf(HEADER2_FORMAT,total_a_read,str_a_read,"",total_a_write,str_a_write,"");
		printf("\n");
	}

	if (config.f.quiet==0||(config.f.quiet==1&&firsthdr)) {
		firsthdr=0;
		printf("%s%6s %4s %-10s %11s",config.f.timestamp?"    TIME ":"",config.f.processes?"PID":"TID","PRIO","USER","DISK READ");
		if (!config.f.hidevfsread)
			printf(" %11s","VFS READ");
		printf(" %11s","DISK WRITE");
		if (!config.f.hidevfswrite)
			printf(" %11s","VFS WRITE");
		printf(" %6s %6s %s\n","SWAPIN","IO","COMMAND");
	}

	iotop_sort(cs,config.f.processes,batch_keep);

	for (i=0;cs->sor&&i<cs->sor_length;i++) {
		struct xxxid_stats *s=cs->sor[i];
		char vfs_read_str[4],vfs_write_str[4];
		char read_str[4],write_str[4];
		double vfs_write_val;
		double vfs_read_val;
		double swapin_val;
		double blkio_val;
		double write_val;
		double read_val;
		char *pw_name;
		char *cmdt;

		if (config.f.accumulated||config.f.accumbw) {
			read_val=accum_val(s,E_ACC_READ,config.f.processes);
			write_val=accum_val(s,E_ACC_WRITE,config.f.processes);
			vfs_read_val=accum_val(s,E_ACC_VFS_READ,config.f.processes);
			vfs_write_val=accum_val(s,E_ACC_VFS_WRITE,config.f.processes);
		} else {
			read_val=(double)(config.f.processes?s->read_diff_p:s->read_diff)/time_s;
			write_val=(double)(config.f.processes?s->write_diff_p:s->write_diff)/time_s;
			vfs_read_val=(double)(config.f.processes?s->read_char_diff_p:s->read_char_diff)/time_s;
			vfs_write_val=(double)(config.f.processes?s->write_char_diff_p:s->write_char_diff)/time_s;
		}
		swapin_val=delay_pc(config.f.processes?s->swapin_diff_p:s->swapin_diff,time_s);
		blkio_val=delay_pc(config.f.processes?s->blkio_diff_p:s->blkio_diff,time_s);

		if (config.f.only&&!read_val&&!write_val&&!swapin_val&&!blkio_val)
			continue;
		if (params.search_regx_ok) {
			int ma_long,ma_short,ma_comm,ma_tid;
			char tid[22];

			sprintf(tid,"%lu",(unsigned long)s->tid);
			ma_long=regexec(&params.search_regx,s->cmdline_long,0,NULL,0);
			ma_short=regexec(&params.search_regx,s->cmdline_short,0,NULL,0);
			if (s->cmdline_comm)
				ma_comm=regexec(&params.search_regx,s->cmdline_comm,0,NULL,0);
			else
				ma_comm=REG_NOMATCH;
			ma_tid=regexec(&params.search_regx,tid,0,NULL,0);
			if (ma_long&&ma_short&&ma_comm&&ma_tid) // nothing matches
				continue;
		}

		humanize_val(&read_val,read_str,1);
		humanize_val(&write_val,write_str,1);
		humanize_val(&vfs_read_val,vfs_read_str,1);
		humanize_val(&vfs_write_val,vfs_write_str,1);

		pw_name=u8strpadt(s->pw_name,10);

		if (config.f.fullcmdline&&s->cmdline_comm&&s->cmdline_long) { // append custom thread name before full cmdline
			char tb[1+strlen(s->cmdline_comm)+1+strlen(s->cmdline_long)+1];

			sprintf(tb,"[%s]%s",s->cmdline_comm,s->cmdline_long);
			cmdt=esc_low_ascii(tb);
		} else
			cmdt=esc_low_ascii(config.f.fullcmdline?s->cmdline_long:s->cmdline_short);

		printf("%s%6i %4s %s %7.2f %-3.3s",tstr,s->tid,str_ioprio(s->io_prio),pw_name?pw_name:"(null)",read_val,read_str);
		if (!config.f.hidevfsread)
			printf(" %7.2f %-3.3s",vfs_read_val,vfs_read_str);
		printf(" %7.2f %-3.3s",write_val,write_str);
		if (!config.f.hidevfswrite)
			printf(" %7.2f %-3.3s",vfs_write_val,vfs_write_str);
		printf(" %2.2f %% %2.2f %% %s\n",swapin_val,blkio_val,cmdt?cmdt:"(null)");

		if (cmdt)
			free(cmdt);

		if (pw_name)
			free(pw_name);
	}
}

inline void view_batch_init(void) {
	if (!read_task_delayacct())
		fprintf(stderr,"Warning: task_delayacct is 0, enable by: echo 1 > /proc/sys/kernel/task_delayacct\n");
}

inline void view_batch_fini(void) {
	if (params.search_str) {
		free(params.search_str);
		params.search_str=NULL;
	}
	if (params.search_regx_ok) {
		regfree(&params.search_regx);
		params.search_regx_ok=0;
	}
	if (params.search_uc) {
		ucell_free(params.search_uc);
		params.search_uc=NULL;
	}
}

inline void view_batch_loop(void) {
	struct xxxid_stats_arr *ps=NULL;
	struct xxxid_stats_arr *cs=NULL;
	struct act_stats act={0,0,0,0,0,0,0,};
	struct timespec ts;
	int warned=0;

	for (;;) {
		cs=fetch_data(ps);
		if (!warned&&taskstats_ver&&taskstats_ver<IOTOP_TASKSTATS_MINVER) { // known after the first query
			fprintf(stderr,"Warning: this kernel provides struct taskstats version %u, which does not contain the required data; it should be %u or greater\n",taskstats_ver,IOTOP_TASKSTATS_MINVER);
			warned=1;
		}
		get_vm_counters(&act.read_bytes,&act.write_bytes);
		act.ts_c=monotime();
		view_batch(cs,ps,&act);

		if (ps)
			arr_free(ps);

		ps=cs;
		act.read_bytes_o=act.read_bytes;
		act.write_bytes_o=act.write_bytes;
		act.ts_o=act.ts_c;
		act.have_o=1;

		if ((params.iter>-1)&&((--params.iter)==0))
			break;
		fflush(stdout);
		ts.tv_sec=params.delay_ms/1000;
		ts.tv_nsec=params.delay_ms%1000*1000000L;
		while (nanosleep(&ts,&ts)==-1&&errno==EINTR) // sleep the rest after a signal
			;
	}
	clear_uid_cache();
	arr_free(cs);
}

