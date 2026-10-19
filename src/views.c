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

struct xxxid_hist_p hist_p_none; // shared empty aggregated history, never written
struct xxxid_hist hist_none={.proc=&hist_p_none}; // shared empty history, never written
int hist_w=HISTORY_CNT; // count of the newest values that the sums in struct xxxid_hist cover

// make sure s has own history to store values in; returns 0 if it can not be allocated
static inline int hist_own(struct xxxid_stats *s) {
	if (s->hist==&hist_none) {
		struct xxxid_hist *h=calloc(1,sizeof *h);

		if (!h)
			return 0;
		h->proc=&hist_p_none;
		s->hist=h;
	}
	return 1;
}

// make sure h has own aggregated history to store values in; returns 0 if it can not be allocated
static inline int hist_own_p(struct xxxid_hist *h) {
	if (h->proc==&hist_p_none) {
		struct xxxid_hist_p *hp=calloc(1,sizeof *hp);

		if (!hp)
			return 0;
		h->proc=hp;
	}
	return 1;
}

// sum of the newest cnt values of history array a of h
inline double hist_sum(const struct xxxid_hist *h,const double *a,int cnt) {
	double sum=0;
	int i,n=0;

	if (cnt>h->cnt) // the rest was never stored
		cnt=h->cnt;
	for (i=h->pos;i>=0&&n<cnt;i--,n++)
		sum+=a[i];
	for (i=HISTORY_CNT-1;n<cnt;i--,n++) // the older part after wrapping around
		sum+=a[i];
	return sum;
}

// sum of the newest cnt values of history array a of h
inline int hist_sum8(const struct xxxid_hist *h,const uint8_t *a,int cnt) {
	int sum=0;
	int i,n=0;

	if (cnt>h->cnt) // the rest was never stored
		cnt=h->cnt;
	for (i=h->pos;i>=0&&n<cnt;i--,n++)
		sum+=a[i];
	for (i=HISTORY_CNT-1;n<cnt;i--,n++) // the older part after wrapping around
		sum+=a[i];
	return sum;
}

// store v as the newest value of a after pos was advanced, keeping the sum of the newest hist_w values
static inline void hist_put(struct xxxid_hist *h,double *a,double *sum,double v) {
	*sum+=v-a[(h->pos+HISTORY_CNT-hist_w)%HISTORY_CNT]; // the value that just left the window
	a[h->pos]=v;
}

static inline void hist_put8(struct xxxid_hist *h,uint8_t *a,int *sum,uint8_t v) {
	*sum+=v-a[(h->pos+HISTORY_CNT-hist_w)%HISTORY_CNT]; // the value that just left the window
	a[h->pos]=v;
}

// make the sums of all histories cover the newest w values; they all belong to the items of cs
inline void hist_window(struct xxxid_stats_arr *cs,int w) {
	int n;

	if (w<1)
		w=1;
	if (w>HISTORY_CNT)
		w=HISTORY_CNT;
	if (w==hist_w)
		return;
	hist_w=w;
	for (n=0;cs->arr&&n<cs->length;n++) {
		struct xxxid_hist *h=cs->arr[n]->hist;

		if (h==&hist_none)
			continue;
		h->iosum=hist_sum8(h,h->iohist,w);
		h->sisum=hist_sum8(h,h->sihist,w);
		h->readsum=hist_sum(h,h->readhist,w);
		h->writesum=hist_sum(h,h->writehist,w);
		if (h->proc!=&hist_p_none) {
			struct xxxid_hist_p *hp=h->proc;

			hp->iosum_p=hist_sum8(h,hp->iohist_p,w);
			hp->sisum_p=hist_sum8(h,hp->sihist_p,w);
			hp->readsum_p=hist_sum(h,hp->readhist_p,w);
			hp->writesum_p=hist_sum(h,hp->writehist_p,w);
		}
	}
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
	double rv,wv,rvp=0,wvp=0;
	int io,si,iop=0,sip=0;

	c->ts_s=p->ts_s; // update end ts
	c->ts_e=ts_c; // update end ts

	// take over the history of the previous cycle
	c->hist=p->hist;
	p->hist=&hist_none;
	h=c->hist;
	if (h!=&hist_none&&c->pid!=c->tid&&h->proc!=&hist_p_none) { // tid of a main process reused by a thread - its aggregated history does not apply
		free(h->proc);
		h->proc=&hist_p_none;
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

	io=value2scale(c->blkio_val,100.0);
	si=value2scale(c->swapin_val,100.0);

	if (c->pid==c->tid) {
		c->blkio_val_p=(double)rrv(c->blkio_delay_total_p,p->blkio_delay_total_p)/(time_s*10000000.0);
		if (c->blkio_val_p>100)
			c->blkio_val_p=100;

		c->swapin_val_p=(double)rrv(c->swapin_delay_total_p,p->swapin_delay_total_p)/(time_s*10000000.0);
		if (c->swapin_val_p>100)
			c->swapin_val_p=100;

		rvp=(double)rrv(c->read_bytes_p,p->read_bytes_p);
		wvp=(double)rrv(c->write_bytes_p,p->write_bytes_p);

		c->read_val_p=rvp/time_s;
		c->write_val_p=wvp/time_s;

		c->read_val_acc_p=p->read_val_acc_p+rvp;
		c->write_val_acc_p=p->write_val_acc_p+wvp;

		c->read_val_abw_p=c->read_val_acc_p/timediff_in_s(c->ts_s,c->ts_e);
		c->write_val_abw_p=c->write_val_acc_p/timediff_in_s(c->ts_s,c->ts_e);

		iop=value2scale(c->blkio_val_p,100.0);
		sip=value2scale(c->swapin_val_p,100.0);
	}

	// an idle task keeps the shared empty history; the older values of a history are all zero,
	// so allocating it with the first nonzero value gives the same result as storing zeros until then
	if (h==&hist_none&&!(io||si||rv>0||wv>0||iop||sip||rvp>0||wvp>0))
		return;
	if (!hist_own(c))
		return;
	h=c->hist;

	// store the values of this cycle as the newest
	h->pos=(h->pos+1)%HISTORY_CNT;
	if (h->cnt<HISTORY_CNT)
		h->cnt++;
	hist_put8(h,h->iohist,&h->iosum,(uint8_t)io);
	hist_put8(h,h->sihist,&h->sisum,(uint8_t)si);
	hist_put(h,h->readhist,&h->readsum,rv);
	hist_put(h,h->writehist,&h->writesum,wv);

	if (c->pid!=c->tid)
		return;
	if (h->proc==&hist_p_none&&!(iop||sip||rvp>0||wvp>0)) // the same for the aggregated history
		return;
	if (hist_own_p(h)) { // pos was already advanced above
		struct xxxid_hist_p *hp=h->proc;

		hist_put8(h,hp->iohist_p,&hp->iosum_p,(uint8_t)iop);
		hist_put8(h,hp->sihist_p,&hp->sisum_p,(uint8_t)sip);
		hist_put(h,hp->readhist_p,&hp->readsum_p,rvp);
		hist_put(h,hp->writehist_p,&hp->writesum_p,wvp);
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
	// pw_name is interned, the pointer is enough
	if (h) { // store zero as the newest values; without own history it is all zero anyway
		h->pos=(h->pos+1)%HISTORY_CNT;
		if (h->cnt<HISTORY_CNT)
			h->cnt++;
		hist_put8(h,h->iohist,&h->iosum,0);
		hist_put8(h,h->sihist,&h->sisum,0);
		hist_put(h,h->readhist,&h->readsum,0.0);
		hist_put(h,h->writehist,&h->writesum,0.0);
		if (p->tid==p->pid&&h->proc!=&hist_p_none) { // process aggregated data, only for main process
			struct xxxid_hist_p *hp=h->proc;

			hist_put8(h,hp->iohist_p,&hp->iosum_p,0);
			hist_put8(h,hp->sihist_p,&hp->sisum_p,0);
			hist_put(h,hp->readhist_p,&hp->readsum_p,0.0);
			hist_put(h,hp->writehist_p,&hp->writesum_p,0.0);
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

			switch (masked_grtype(0)) {
				case E_GR_IO:
					if (grlen==0)
						grlen=HISTORY_CNT;
					aa=HIST_SUM8(pa->hist,io,config.f.processes,grlen);
					ab=HIST_SUM8(pb->hist,io,config.f.processes,grlen);
					res=aa-ab;
					break;
				case E_GR_R:
					if (grlen==0)
						grlen=HISTORY_CNT;
					da=HIST_SUM(pa->hist,read,config.f.processes,grlen);
					db=HIST_SUM(pb->hist,read,config.f.processes,grlen);
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
					da=HIST_SUM(pa->hist,write,config.f.processes,grlen);
					db=HIST_SUM(pb->hist,write,config.f.processes,grlen);
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
					da=HIST_SUM(pa->hist,read,config.f.processes,grlen)+HIST_SUM(pa->hist,write,config.f.processes,grlen);
					db=HIST_SUM(pb->hist,read,config.f.processes,grlen)+HIST_SUM(pb->hist,write,config.f.processes,grlen);
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
					aa=HIST_SUM8(pa->hist,si,config.f.processes,grlen);
					ab=HIST_SUM8(pb->hist,si,config.f.processes,grlen);
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

	if ((params.user_id!=-1)&&(s->euid!=(uid_t)params.user_id))
		return 1;

	if ((params.pid!=-1)&&(s->tid!=params.pid))
		return 1;

	return 0;
}
