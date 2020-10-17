/* SPDX-License-Identifer: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020  Boian Bonev

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

#include "iotop.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

inline void calc_total(struct xxxid_stats_arr *cs,double *read,double *write) {
	int i;

	*read=*write=0;

	for (i=0;i<cs->length;i++) {
		if (!config.f.accumulated) {
			*read+=cs->arr[i]->read_val;
			*write+=cs->arr[i]->write_val;
		} else {
			*read+=cs->arr[i]->read_val_acc;
			*write+=cs->arr[i]->write_val_acc;
		}
	}
}

#define RRV(to,from) (((to)<(from))?(~0ULL)-(to)+(from):(to)-(from))
#define RRVf(pto,pfrom,fld) RRV(pto->fld,pfrom->fld)

inline void calc_a_total(struct act_stats *act,double *read,double *write,double time_s) {
	*read=*write=0;

	if (act->have_o) {
		uint64_t r=act->read_bytes;
		uint64_t w=act->write_bytes;

		r=RRV(r,act->read_bytes_o);
		w=RRV(w,act->write_bytes_o);
		*read=(double)r/time_s;
		*write=(double)w/time_s;
	}
}

inline int value2scale(double val,double mx) {
	val=100.0*val/mx;

	if (val>75)
		return 4;
	if (val>50)
		return 3;
	if (val>25)
		return 2;
	if (val>0)
		return 1;
	return 0;
}

inline int create_diff(struct xxxid_stats_arr *cs,struct xxxid_stats_arr *ps,double time_s) {
	int diff_size=cs->length;
	int n=0;

	for (n=0;cs->arr&&n<cs->length;n++) {
		struct xxxid_stats *c;
		struct xxxid_stats *p;
		double rv,wv;
		char temp[12];

		c=cs->arr[n];
		p=arr_find(ps,c->tid);

		if (!p) { // new process or task
			c->blkio_val=0;
			c->swapin_val=0;
			c->read_val=0;
			c->write_val=0;
			c->read_val_acc=0;
			c->write_val_acc=0;

			snprintf(temp,sizeof temp,"%i",c->tid);
			maxpidlen=maxpidlen<(int)strlen(temp)?(int)strlen(temp):maxpidlen;
			continue;
		}

		// round robin value
		c->blkio_val=(double)RRVf(c,p,blkio_delay_total)/(time_s*10000000.0);
		if (c->blkio_val>100)
			c->blkio_val=100;

		c->swapin_val=(double)RRVf(c,p,swapin_delay_total)/(time_s*10000000.0);
		if (c->swapin_val>100)
			c->swapin_val=100;

		rv=(double)RRVf(c,p,read_bytes);
		wv=(double)RRVf(c,p,write_bytes);

		c->read_val=rv/time_s;
		c->write_val=wv/time_s;

		c->read_val_acc=p->read_val_acc+rv;
		c->write_val_acc=p->write_val_acc+wv;

		memcpy(c->iohist+1,p->iohist,sizeof c->iohist-sizeof *c->iohist);
		c->iohist[0]=value2scale(c->blkio_val,100.0);

		snprintf(temp,sizeof temp,"%i",c->tid);
		maxpidlen=maxpidlen<(int)strlen(temp)?(int)strlen(temp):maxpidlen;
	}
	for (n=0;ps->arr&&n<ps->length;n++) { // copy old data for exited processes
		if (ps->arr[n]->exited||!arr_find(cs,ps->arr[n]->tid)) {
			struct xxxid_stats *p;

			ps->arr[n]->exited++;
			if (ps->arr[n]->exited>HISTORY_CNT)
				continue;
			// last state is zero, only history remains
			ps->arr[n]->blkio_val=0;
			ps->arr[n]->swapin_val=0;
			ps->arr[n]->read_val=0;
			ps->arr[n]->write_val=0;
			ps->arr[n]->read_val_acc=0;
			ps->arr[n]->write_val_acc=0;
			// copy process data to cs
			p=malloc(sizeof *p);
			if (p) {
				*p=*ps->arr[n];
				// copy dynamic data to avoid double free; in the unlikely case strdup fails, data is just lost
				if (p->cmdline1)
					p->cmdline1=strdup(ps->arr[n]->cmdline1);
				if (p->cmdline2)
					p->cmdline2=strdup(ps->arr[n]->cmdline2);
				if (p->pw_name)
					p->pw_name=strdup(ps->arr[n]->pw_name);
				// shift history one step
				memmove(p->iohist+1,p->iohist,sizeof p->iohist-sizeof *p->iohist);
				p->iohist[0]=0;
				if (arr_add(cs,p)) { // free the data in case add fails
					if (p->cmdline1)
						free(p->cmdline1);
					if (p->cmdline2)
						free(p->cmdline2);
					if (p->pw_name)
						free(p->pw_name);
					free(p);
				}
			}
		}
	}

	return diff_size;
}

inline void humanize_val(double *value,char *str,int allow_accum) {
	const char *u="BKMGTPEZY";
	size_t p=0;

	if (config.f.kilobytes) {
		p=1;
		*value/=1000.0;
	} else {
		while (*value>10000) {
			if (p+1<strlen(u)) {
				*value/=1000.0;
				p++;
			} else
				break;
		}
	}

	snprintf(str,4,"%c%s",u[p],config.f.accumulated&&allow_accum?"  ":"/s");
}

inline int iotop_sort_cb(const void *a,const void *b) {
	int order=config.f.sort_order?1:-1; // SORT_ASC is bit 0=1, else should reverse sort
	struct xxxid_stats **ppa=(struct xxxid_stats **)a;
	struct xxxid_stats **ppb=(struct xxxid_stats **)b;
	struct xxxid_stats *pa,*pb;
	int type=config.f.sort_by;
	static int grlen=0;
	int res=0;

	if (!a) {
		grlen=(long)b;
		return 0;
	}

	pa=*ppa;
	pb=*ppb;

	switch (type) {
		case SORT_BY_GRAPH: {
			int aa=0,ab=0;
			int i;

			if (grlen==0)
				grlen=HISTORY_CNT;
			for (i=0;i<grlen;i++) {
				aa+=pa->iohist[i];
				ab+=pb->iohist[i];
			}
			res=aa-ab;
			break;
		}
		case SORT_BY_PRIO:
			res=pa->io_prio-pb->io_prio;
			break;
		case SORT_BY_COMMAND:
			res=strcmp(config.f.fullcmdline?pa->cmdline2:pa->cmdline1,config.f.fullcmdline?pb->cmdline2:pb->cmdline1);
			break;
		case SORT_BY_PID:
			res=pa->tid-pb->tid;
			break;
		case SORT_BY_USER:
			res=strcmp(pa->pw_name,pb->pw_name);
			break;
		case SORT_BY_READ:
			if (config.f.accumulated)
				res=pa->read_val_acc>pb->read_val_acc?1:pa->read_val_acc<pb->read_val_acc?-1:0;
			else
				res=pa->read_val>pb->read_val?1:pa->read_val<pb->read_val?-1:0;
			break;
		case SORT_BY_WRITE:
			if (config.f.accumulated)
				res=pa->write_val_acc>pb->write_val_acc?1:pa->write_val_acc<pb->write_val_acc?-1:0;
			else
				res=pa->write_val>pb->write_val?1:pa->write_val<pb->write_val?-1:0;
			break;
		case SORT_BY_SWAPIN:
			res=pa->swapin_val>pb->swapin_val?1:pa->swapin_val<pb->swapin_val?-1:0;
			break;
		case SORT_BY_IO:
			res=pa->blkio_val>pb->blkio_val?1:pa->blkio_val<pb->blkio_val?-1:0;
			break;
	}
	res*=order;
	return res;
}

inline int filter1(struct xxxid_stats *s) {
	if ((params.user_id!=-1)&&(s->euid!=params.user_id))
		return 1;

	if ((params.pid!=-1)&&(s->tid!=params.pid))
		return 1;

	return 0;
}

