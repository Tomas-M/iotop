/* SPDX-License-Identifier: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020-2026  Boian Bonev

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

struct xxxid_hist hist_none; // shared empty history, never written

// make sure s has own history to store values in; returns 0 if it can not be allocated
static inline int hist_own(struct xxxid_stats *s) {
	if (s->hist==&hist_none) {
		struct xxxid_hist *h=calloc(1,sizeof *h);

		if (!h)
			return 0;
		s->hist=h;
	}
	return 1;
}

// keep the longest tid length for the display
static inline void update_maxpidlen(pid_t tid) {
	int len=1;

	for (;tid>=10;tid/=10)
		len++;
	if (maxpidlen<len)
		maxpidlen=len;
}

// new process or task - there is no previous data to diff against
static inline void stats_new(struct xxxid_stats *c,uint64_t ts_c) {
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
}

// calculate the values of the current cycle from the previous data in p
static inline void stats_diff(struct xxxid_stats *c,struct xxxid_stats *p,double time_s,uint64_t ts_c) {
	struct xxxid_hist *h;
	double rv,wv;

	c->ts_s=p->ts_s; // update end ts
	c->ts_e=ts_c; // update end ts

	// take over the history of the previous cycle
	c->hist=p->hist;
	p->hist=&hist_none;
	h=hist_own(c)?c->hist:NULL;
	if (h&&c->pid!=c->tid&&p->pid==p->tid) { // tid of a main process reused by a thread - its aggregated history does not apply
		memset(h->iohist_p,0,sizeof h->iohist_p);
		memset(h->sihist_p,0,sizeof h->sihist_p);
		memset(h->readhist_p,0,sizeof h->readhist_p);
		memset(h->writehist_p,0,sizeof h->writehist_p);
	}

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

	if (h) { // shift history one step
		memmove(h->iohist+1,h->iohist,sizeof h->iohist-sizeof *h->iohist);
		h->iohist[0]=value2scale(c->blkio_val,100.0);
		memmove(h->sihist+1,h->sihist,sizeof h->sihist-sizeof *h->sihist);
		h->sihist[0]=value2scale(c->swapin_val,100.0);
		memmove(h->readhist+1,h->readhist,sizeof h->readhist-sizeof *h->readhist);
		h->readhist[0]=rv;
		memmove(h->writehist+1,h->writehist,sizeof h->writehist-sizeof *h->writehist);
		h->writehist[0]=wv;
	}

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

		if (h) {
			memmove(h->iohist_p+1,h->iohist_p,sizeof h->iohist_p-sizeof *h->iohist_p);
			h->iohist_p[0]=value2scale(c->blkio_val_p,100.0);
			memmove(h->sihist_p+1,h->sihist_p,sizeof h->sihist_p-sizeof *h->sihist_p);
			h->sihist_p[0]=value2scale(c->swapin_val_p,100.0);
			memmove(h->readhist_p+1,h->readhist_p,sizeof h->readhist_p-sizeof *h->readhist_p);
			h->readhist_p[0]=rv;
			memmove(h->writehist_p+1,h->writehist_p,sizeof h->writehist_p-sizeof *h->writehist_p);
			h->writehist_p[0]=wv;
		}
	}
}

// exited process or task - make a copy of o that keeps only the history
static inline struct xxxid_stats *stats_exited(struct xxxid_stats *o) {
	struct xxxid_stats *p=malloc(sizeof *p);
	struct xxxid_hist *h;

	if (!p)
		return NULL;
	*p=*o; // WARNING - all dynamic data inside should always be initialized below
	p->threads=NULL;
	// take over the history
	o->hist=&hist_none;
	h=p->hist!=&hist_none?p->hist:NULL;
	p->exited++;
	// last state is zero, only history remains
	p->blkio_val=0;
	p->swapin_val=0;
	p->read_val=0;
	p->write_val=0;
	// copy dynamic data to avoid double free; in the unlikely event when strdup fails, filter1 will skip this item
	if (p->cmdline_short)
		p->cmdline_short=strdup(o->cmdline_short);
	if (p->cmdline_long)
		p->cmdline_long=strdup(o->cmdline_long);
	if (p->cmdline_comm)
		p->cmdline_comm=strdup(o->cmdline_comm);
	if (p->pw_name)
		p->pw_name=strdup(o->pw_name);
	if (h) { // shift history one step; without own history it is all zero anyway
		memmove(h->iohist+1,h->iohist,sizeof h->iohist-sizeof *h->iohist);
		h->iohist[0]=0;
		memmove(h->sihist+1,h->sihist,sizeof h->sihist-sizeof *h->sihist);
		h->sihist[0]=0;
		memmove(h->readhist+1,h->readhist,sizeof h->readhist-sizeof *h->readhist);
		h->readhist[0]=0.0;
		memmove(h->writehist+1,h->writehist,sizeof h->writehist-sizeof *h->writehist);
		h->writehist[0]=0.0;
		if (p->tid==p->pid) { // shift process aggregated data, only for main process
			memmove(h->iohist_p+1,h->iohist_p,sizeof h->iohist_p-sizeof *h->iohist_p);
			h->iohist_p[0]=0;
			memmove(h->sihist_p+1,h->sihist_p,sizeof h->sihist_p-sizeof *h->sihist_p);
			h->sihist_p[0]=0;
			memmove(h->readhist_p+1,h->readhist_p,sizeof h->readhist_p-sizeof *h->readhist_p);
			h->readhist_p[0]=0.0;
			memmove(h->writehist_p+1,h->writehist_p,sizeof h->writehist_p-sizeof *h->writehist_p);
			h->writehist_p[0]=0.0;
		}
	}
	return p;
}

inline int create_diff(struct xxxid_stats_arr *cs,struct xxxid_stats_arr *ps,double time_s,uint64_t ts_c) {
	struct xxxid_stats **ex=NULL; // copies of the processes and tasks that exited
	int exn=0;
	int ncs,nps;
	int n,x,y;

	if (cs->diffed) // the data of this cycle is already done; redraws do not need it again
		return cs->length;
	cs->diffed=1;

	ncs=cs->arr?cs->length:0;
	nps=ps&&ps->arr?ps->length:0;

	// both arrays are kept sorted by tid, walk them together to find the pairs
	for (x=0,y=0;x<ncs||y<nps;) {
		if (x<ncs&&(y>=nps||cs->arr[x]->tid<ps->arr[y]->tid)) { // new process or task
			stats_new(cs->arr[x],ts_c);
			update_maxpidlen(cs->arr[x]->tid);
			x++;
			continue;
		}
		if (x>=ncs||ps->arr[y]->tid<cs->arr[x]->tid) { // exited process or task
			// there is no point to keep data for the ones gone for more than HISTORY_CNT cycles
			if (ps->arr[y]->exited<HISTORY_CNT) {
				struct xxxid_stats *e;

				if (!ex) // at most the remaining items of ps can exit
					ex=malloc((nps-y)*sizeof *ex);
				if (ex&&(e=stats_exited(ps->arr[y])))
					ex[exn++]=e;
			}
			y++;
			continue;
		}
		// present in both cycles
		stats_diff(cs->arr[x],ps->arr[y],time_s,ts_c);
		update_maxpidlen(cs->arr[x]->tid);
		x++;
		y++;
	}

	if (exn&&arr_merge(cs,ex,exn)) // the copies are in tid order and none of them is in cs
		for (n=0;n<exn;n++) // free the data in case merge fails
			free_stats(ex[n]);
	if (ex)
		free(ex);

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
	}

	return cs->length;
}

// count the lines that the view shows - the processes and their threads that pass the filter
inline int count_visible(struct xxxid_stats_arr *cs,filter_callback_w cb,int width) {
	int cnt=0;
	int n,k;

	for (n=0;cs->arr&&n<cs->length;n++) {
		struct xxxid_stats *c=cs->arr[n];

		if (c->pid!=c->tid) // threads are shown only below their process
			continue;
		if (!cb(c,width))
			cnt++;
		if (config.f.processes)
			continue;
		for (k=0;c->threads&&c->threads->arr&&k<c->threads->length;k++)
			if (!cb(c->threads->arr[k],width))
				cnt++;
	}

	return cnt;
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
						aa+=config.f.processes?pa->hist->iohist_p[i]:pa->hist->iohist[i];
						ab+=config.f.processes?pb->hist->iohist_p[i]:pb->hist->iohist[i];
					}
					res=aa-ab;
					break;
				case E_GR_R:
					if (grlen==0)
						grlen=HISTORY_CNT;
					for (i=0;i<grlen;i++) {
						da+=config.f.processes?pa->hist->readhist_p[i]:pa->hist->readhist[i];
						db+=config.f.processes?pb->hist->readhist_p[i]:pb->hist->readhist[i];
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
						da+=config.f.processes?pa->hist->writehist_p[i]:pa->hist->writehist[i];
						db+=config.f.processes?pb->hist->writehist_p[i]:pb->hist->writehist[i];
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
						da+=config.f.processes?pa->hist->readhist_p[i]:pa->hist->readhist[i];
						db+=config.f.processes?pb->hist->readhist_p[i]:pb->hist->readhist[i];
						da+=config.f.processes?pa->hist->writehist_p[i]:pa->hist->writehist[i];
						db+=config.f.processes?pb->hist->writehist_p[i]:pb->hist->writehist[i];
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
						aa+=config.f.processes?pa->hist->sihist_p[i]:pa->hist->sihist[i];
						ab+=config.f.processes?pb->hist->sihist_p[i]:pb->hist->sihist[i];
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
			res=strcmp(config.f.fullcmdline?pa->cmdline_long:pa->cmdline_short,config.f.fullcmdline?pb->cmdline_long:pb->cmdline_short);
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
	if (!s->cmdline_long||!s->cmdline_short) // if strdup fails during copy, those may become NULL
		return 1;

	if ((params.user_id!=-1)&&(s->euid!=params.user_id))
		return 1;

	if ((params.pid!=-1)&&(s->tid!=params.pid))
		return 1;

	return 0;
}
