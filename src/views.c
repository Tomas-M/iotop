/* SPDX-License-Identifier: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020-2024  Boian Bonev

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

static inline uint64_t rrv(uint64_t to,uint64_t from) {
	uint64_t result;

	if (to<from) {
		result=0;
		result=~result;
		result-=to;
		result+=from;
	} else {
		result=0;
		result+=to;
		result-=from;
	}
	return result;
}

inline void calc_a_total(struct act_stats *act,double *read,double *write,double time_s) {
	*read=*write=0;

	if (act->have_o) {
		uint64_t r=act->read_bytes;
		uint64_t w=act->write_bytes;

		r=rrv(r,act->read_bytes_o);
		w=rrv(w,act->write_bytes_o);
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

inline int create_diff(struct xxxid_stats_arr *cs,struct xxxid_stats_arr *ps,double time_s,uint64_t ts_c,filter_callback_w cb,int width,int *cnt) {
	int n=0;

	if (cnt)
		*cnt=0;
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
			c->read_val_abw=0;
			c->write_val_abw=0;
			c->ts_s=ts_c; // keep start ts
			c->ts_e=ts_c; // keep end ts

			snprintf(temp,sizeof temp,"%i",c->tid);
			maxpidlen=maxpidlen<(int)strlen(temp)?(int)strlen(temp):maxpidlen;
			continue;
		}
		c->ts_s=p->ts_s; // update end ts
		c->ts_e=ts_c; // update end ts

		// round robin value
		c->blkio_val=(double)rrv(c->blkio_delay_total,p->blkio_delay_total)/(time_s*10000000.0);
		if (c->blkio_val>100)
			c->blkio_val=100;

		c->swapin_val=(double)rrv(c->swapin_delay_total,p->swapin_delay_total)/(time_s*10000000.0);
		if (c->swapin_val>100)
			c->swapin_val=100;

		rv=(double)rrv(c->read_bytes,p->read_bytes);
		wv=(double)rrv(c->write_bytes,p->write_bytes);

		c->read_val=rv/time_s;
		c->write_val=wv/time_s;

		c->read_val_acc=p->read_val_acc+rv;
		c->write_val_acc=p->write_val_acc+wv;

		c->read_val_abw=c->read_val_acc/timediff_in_s(c->ts_s,c->ts_e);
		c->write_val_abw=c->write_val_acc/timediff_in_s(c->ts_s,c->ts_e);

		memcpy(c->iohist+1,p->iohist,sizeof c->iohist-sizeof *c->iohist);
		c->iohist[0]=value2scale(c->blkio_val,100.0);
		memcpy(c->sihist+1,p->sihist,sizeof c->sihist-sizeof *c->sihist);
		c->sihist[0]=value2scale(c->swapin_val,100.0);
		memcpy(c->readhist+1,p->readhist,sizeof c->readhist-sizeof *c->readhist);
		c->readhist[0]=rv;
		memcpy(c->writehist+1,p->writehist,sizeof c->writehist-sizeof *c->writehist);
		c->writehist[0]=wv;

		if (c->pid==c->tid) {
			c->blkio_val_p=(double)rrv(c->blkio_delay_total_p,p->blkio_delay_total_p)/(time_s*10000000.0);
			if (c->blkio_val_p>100)
				c->blkio_val_p=100;

			c->swapin_val_p=(double)rrv(c->swapin_delay_total_p,p->swapin_delay_total_p)/(time_s*10000000.0);
			if (c->swapin_val_p>100)
				c->swapin_val_p=100;

			rv=(double)rrv(c->read_bytes_p,p->read_bytes_p);
			wv=(double)rrv(c->write_bytes_p,p->write_bytes_p);

			c->read_val_p=rv/time_s;
			c->write_val_p=wv/time_s;

			c->read_val_acc_p=p->read_val_acc_p+rv;
			c->write_val_acc_p=p->write_val_acc_p+wv;

			c->read_val_abw_p=c->read_val_acc_p/timediff_in_s(c->ts_s,c->ts_e);
			c->write_val_abw_p=c->write_val_acc_p/timediff_in_s(c->ts_s,c->ts_e);

			memcpy(c->iohist_p+1,p->iohist_p,sizeof c->iohist_p-sizeof *c->iohist_p);
			c->iohist_p[0]=value2scale(c->blkio_val_p,100.0);
			memcpy(c->sihist_p+1,p->sihist_p,sizeof c->sihist_p-sizeof *c->sihist_p);
			c->sihist_p[0]=value2scale(c->swapin_val_p,100.0);
			memcpy(c->readhist_p+1,p->readhist_p,sizeof c->readhist_p-sizeof *c->readhist_p);
			c->readhist_p[0]=rv;
			memcpy(c->writehist_p+1,p->writehist_p,sizeof c->writehist_p-sizeof *c->writehist_p);
			c->writehist_p[0]=wv;
		}

		snprintf(temp,sizeof temp,"%i",c->tid);
		maxpidlen=maxpidlen<(int)strlen(temp)?(int)strlen(temp):maxpidlen;
	}
	for (n=0;ps&&ps->arr&&n<ps->length;n++) { // copy old data for exited processes
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
			// copy process data to cs
			p=malloc(sizeof *p);
			if (p) {
				*p=*ps->arr[n]; // WARNING - all dynamic data inside should always be initialized below
				p->threads=NULL;
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
				memmove(p->sihist+1,p->sihist,sizeof p->sihist-sizeof *p->sihist);
				p->sihist[0]=0;
				memmove(p->readhist+1,p->readhist,sizeof p->readhist-sizeof *p->readhist);
				p->readhist[0]=0.0;
				memmove(p->writehist+1,p->writehist,sizeof p->writehist-sizeof *p->writehist);
				p->writehist[0]=0.0;
				if (p->tid==p->pid) { // shift process aggregated data, only for main process
					memmove(p->iohist_p+1,p->iohist_p,sizeof p->iohist_p-sizeof *p->iohist_p);
					p->iohist_p[0]=0;
					memmove(p->sihist_p+1,p->sihist_p,sizeof p->sihist_p-sizeof *p->sihist_p);
					p->sihist_p[0]=0;
					memmove(p->readhist_p+1,p->readhist_p,sizeof p->readhist_p-sizeof *p->readhist_p);
					p->readhist_p[0]=0.0;
					memmove(p->writehist_p+1,p->writehist_p,sizeof p->writehist_p-sizeof *p->writehist_p);
					p->writehist_p[0]=0.0;
				}
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
	// reattach exited threads back to their original process
	for (n=0;cs->arr&&n<cs->length;n++) {
		struct xxxid_stats *c;

		c=cs->arr[n];
		if (c->pid!=c->tid&&c->exited) {
			struct xxxid_stats *p;

			p=arr_find(cs,c->pid); // pid==tid for the main process
			if (!p)
				continue;
			if (!p->threads)
				p->threads=arr_alloc();
			if (!p->threads) // ignore the old data in case of memory alloc error
				continue;
			arr_add(p->threads,c);
		}
		if (cb&&!cb(c,width))
			if (cnt)
				(*cnt)++;
	}

	return cs->length;
}

inline void humanize_val(double *value,char *str,int allow_accum) {
	const char *u="BKMGTPEZY";
	size_t p=0;

	if (config.f.kilobytes) {
		p=1;
		*value/=(double)config.f.base;
	} else {
		while (*value>config.f.base*config.f.threshold) {
			if (p+1<strlen(u)) {
				*value/=(double)config.f.base;
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
	static int grlen=0;
	int res=0;

	if (!a) {
		grlen=(long)b;
		return 0;
	}

	pa=*ppa;
	pb=*ppb;

	switch (masked_sort_by(0)) {
		case SORT_BY_GRAPH: {
			double da=0,db=0;
			int aa=0,ab=0;
			int i;

			switch (masked_grtype(0)) {
				case E_GR_IO:
					if (grlen==0)
						grlen=HISTORY_CNT;
					for (i=0;i<grlen;i++) {
						aa+=config.f.processes?pa->iohist_p[i]:pa->iohist[i];
						ab+=config.f.processes?pb->iohist_p[i]:pb->iohist[i];
					}
					res=aa-ab;
					break;
				case E_GR_R:
					if (grlen==0)
						grlen=HISTORY_CNT;
					for (i=0;i<grlen;i++) {
						da+=config.f.processes?pa->readhist_p[i]:pa->readhist[i];
						db+=config.f.processes?pb->readhist_p[i]:pb->readhist[i];
					}
					if (da>db)
						res=1;
					else if (da<db)
						res=-1;
					else
						res=0;
					break;
				case E_GR_W:
					if (grlen==0)
						grlen=HISTORY_CNT;
					for (i=0;i<grlen;i++) {
						da+=config.f.processes?pa->writehist_p[i]:pa->writehist[i];
						db+=config.f.processes?pb->writehist_p[i]:pb->writehist[i];
					}
					if (da>db)
						res=1;
					else if (da<db)
						res=-1;
					else
						res=0;
					break;
				case E_GR_RW:
					if (grlen==0)
						grlen=HISTORY_CNT;
					for (i=0;i<grlen;i++) {
						da+=config.f.processes?pa->readhist_p[i]:pa->readhist[i];
						db+=config.f.processes?pb->readhist_p[i]:pb->readhist[i];
						da+=config.f.processes?pa->writehist_p[i]:pa->writehist[i];
						db+=config.f.processes?pb->writehist_p[i]:pb->writehist[i];
					}
					if (da>db)
						res=1;
					else if (da<db)
						res=-1;
					else
						res=0;
					break;
				case E_GR_SW:
					if (grlen==0)
						grlen=HISTORY_CNT;
					for (i=0;i<grlen;i++) {
						aa+=config.f.processes?pa->sihist_p[i]:pa->sihist[i];
						ab+=config.f.processes?pb->sihist_p[i]:pb->sihist[i];
					}
					res=aa-ab;
					break;
			}
			break;
		}
		case SORT_BY_PRIO:
			res=pa->io_prio-pb->io_prio;
			break;
		case SORT_BY_COMMAND:
			res=strcmp(config.f.fullcmdline?pa->cmdline2:pa->cmdline1,config.f.fullcmdline?pb->cmdline2:pb->cmdline1);
			break;
		case SORT_BY_TID:
			res=pa->tid-pb->tid;
			break;
		case SORT_BY_USER:
			res=strcmp(pa->pw_name,pb->pw_name);
			break;
		case SORT_BY_READ:
			if (config.f.accumbw)
				res=(config.f.processes?pa->read_val_abw_p:pa->read_val_abw)>(config.f.processes?pb->read_val_abw_p:pb->read_val_abw)?1:
					(config.f.processes?pa->read_val_abw_p:pa->read_val_abw)<(config.f.processes?pb->read_val_abw_p:pb->read_val_abw)?-1:0;
			else if (config.f.accumulated)
				res=(config.f.processes?pa->read_val_acc_p:pa->read_val_acc)>(config.f.processes?pb->read_val_acc_p:pb->read_val_acc)?1:
					(config.f.processes?pa->read_val_acc_p:pa->read_val_acc)<(config.f.processes?pb->read_val_acc_p:pb->read_val_acc)?-1:0;
			else
				res=(config.f.processes?pa->read_val_p:pa->read_val)>(config.f.processes?pb->read_val_p:pb->read_val)?1:
					(config.f.processes?pa->read_val_p:pa->read_val)<(config.f.processes?pb->read_val_p:pb->read_val)?-1:0;
			break;
		case SORT_BY_WRITE:
			if (config.f.accumbw)
				res=(config.f.processes?pa->write_val_abw_p:pa->write_val_abw)>(config.f.processes?pb->write_val_abw_p:pb->write_val_abw)?1:
					(config.f.processes?pa->write_val_abw_p:pa->write_val_abw)<(config.f.processes?pb->write_val_abw_p:pb->write_val_abw)?-1:0;
			else if (config.f.accumulated)
				res=(config.f.processes?pa->write_val_acc_p:pa->write_val_acc)>(config.f.processes?pb->write_val_acc_p:pb->write_val_acc)?1:
					(config.f.processes?pa->write_val_acc_p:pa->write_val_acc)<(config.f.processes?pb->write_val_acc_p:pb->write_val_acc)?-1:0;
			else
				res=(config.f.processes?pa->write_val_p:pa->write_val)>(config.f.processes?pb->write_val_p:pb->write_val)?1:
					(config.f.processes?pa->write_val_p:pa->write_val)<(config.f.processes?pb->write_val_p:pb->write_val)?-1:0;
			break;
		case SORT_BY_SWAPIN:
			res=(config.f.processes?pa->swapin_val_p:pa->swapin_val)>(config.f.processes?pb->swapin_val_p:pb->swapin_val)?1:
				(config.f.processes?pa->swapin_val_p:pa->swapin_val)<(config.f.processes?pb->swapin_val_p:pb->swapin_val)?-1:0;
			break;
		case SORT_BY_IO:
			res=(config.f.processes?pa->blkio_val_p:pa->blkio_val)>(config.f.processes?pb->blkio_val_p:pb->blkio_val)?1:
				(config.f.processes?pa->blkio_val_p:pa->blkio_val)<(config.f.processes?pb->blkio_val_p:pb->blkio_val)?-1:0;
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
