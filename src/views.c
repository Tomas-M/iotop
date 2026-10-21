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

// growth of a counter between two cycles; the 64-bit counters do not wrap, a decrease is not growth:
// the sums of a process lose the counters of its exited threads, and a new task may get the tid of an old one
inline uint64_t rrv(uint64_t to,uint64_t from) {
	return to<from?0:to-from;
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
	c->read_char_val=0;
	c->write_char_val=0;
	c->read_val_acc=0;
	c->write_val_acc=0;
	c->read_char_val_acc=0;
	c->write_char_val_acc=0;
	c->ts_s=ts_c; // keep start ts
	c->ts_e=ts_c; // keep end ts
}

// calculate the values of the current cycle from the previous data in p
static inline void stats_diff(struct xxxid_stats *c,struct xxxid_stats *p,double time_s,uint64_t ts_c) {
	struct xxxid_hist *h;
	double rv,wv,rcv,wcv,rvp=0,wvp=0;
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
	rcv=(double)rrv(c->read_char,p->read_char);
	wcv=(double)rrv(c->write_char,p->write_char);

	c->read_val=rv/time_s;
	c->write_val=wv/time_s;
	c->read_char_val=rcv/time_s;
	c->write_char_val=wcv/time_s;

	c->read_val_acc=p->read_val_acc+rv;
	c->write_val_acc=p->write_val_acc+wv;
	c->read_char_val_acc=p->read_char_val_acc+rcv;
	c->write_char_val_acc=p->write_char_val_acc+wcv;

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
		rcv=(double)rrv(c->read_char_p,p->read_char_p);
		wcv=(double)rrv(c->write_char_p,p->write_char_p);

		c->read_val_p=rvp/time_s;
		c->write_val_p=wvp/time_s;
		c->read_char_val_p=rcv/time_s;
		c->write_char_val_p=wcv/time_s;

		c->read_val_acc_p=p->read_val_acc_p+rvp;
		c->write_val_acc_p=p->write_val_acc_p+wvp;
		c->read_char_val_acc_p=p->read_char_val_acc_p+rcv;
		c->write_char_val_acc_p=p->write_char_val_acc_p+wcv;

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

// exited process or task - o moves from the previous cycle to the current one and keeps only its history
static inline struct xxxid_stats *stats_exited(struct xxxid_stats *o) {
	struct xxxid_hist *h=o->hist!=&hist_none?o->hist:NULL;

	arr_free_noitem(o->threads); // items of the previous cycle; the exited threads are attached again from the current one
	o->threads=NULL;
	o->exited++;
	// last state is zero, only history remains
	o->blkio_val=0;
	o->swapin_val=0;
	o->read_val=0;
	o->write_val=0;
	o->read_char_val=0;
	o->write_char_val=0;
	if (h) { // store zero as the newest values; without own history it is all zero anyway
		h->pos=(h->pos+1)%HISTORY_CNT;
		if (h->cnt<HISTORY_CNT)
			h->cnt++;
		hist_put8(h,h->iohist,&h->iosum,0);
		hist_put8(h,h->sihist,&h->sisum,0);
		hist_put(h,h->readhist,&h->readsum,0.0);
		hist_put(h,h->writehist,&h->writesum,0.0);
		if (o->tid==o->pid&&h->proc!=&hist_p_none) { // process aggregated data, only for main process
			struct xxxid_hist_p *hp=h->proc;

			hist_put8(h,hp->iohist_p,&hp->iosum_p,0);
			hist_put8(h,hp->sihist_p,&hp->sisum_p,0);
			hist_put(h,hp->readhist_p,&hp->readsum_p,0.0);
			hist_put(h,hp->writehist_p,&hp->writesum_p,0.0);
		}
	}
	return o;
}

// c and p have the same tid; tell if c is the task of p and not a new one that got its tid
inline int same_task(const struct xxxid_stats *c,const struct xxxid_stats *p) {
	if (p->exited) // the tid of a kept exited task was given to a new one
		return 0;
	if (!c->btime||!p->btime) // the start time is known only from taskstats v12
		return 1;
	// the kernel gives the start time as the current second minus the elapsed seconds, both rounded down,
	// so for the same task it may differ by one between two cycles
	return c->btime<=p->btime+1&&p->btime<=c->btime+1;
}

// order the exited threads by process, keeping them in tid order within it
static int exited_thread_cmp(const void *a,const void *b) {
	const struct xxxid_stats *ta=*(struct xxxid_stats * const *)a;
	const struct xxxid_stats *tb=*(struct xxxid_stats * const *)b;

	if (ta->pid!=tb->pid)
		return ta->pid<tb->pid?-1:1;
	return ta->tid<tb->tid?-1:ta->tid>tb->tid;
}

// exited tasks move to cs while their exited count stays below keep_exited; ps is only freed afterwards
inline int create_diff(struct xxxid_stats_arr *cs,struct xxxid_stats_arr *ps,double time_s,uint64_t ts_c,int keep_exited) {
	struct xxxid_stats **ex=NULL; // the processes and tasks that exited, moved from ps
	struct xxxid_stats **et=NULL; // the exited threads in cs
	int exn=0,etn=0;
	int ncs,nps;
	int n,x,y,e;

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
			if (ps->arr[y]->exited+1<keep_exited) {
				if (!ex) // at most the remaining items of ps can exit
					ex=malloc((nps-y)*sizeof *ex);
				if (ex) {
					ex[exn++]=stats_exited(ps->arr[y]);
					ps->arr[y]=NULL; // cs owns it now
				}
			}
			y++;
			continue;
		}
		// the same tid in both cycles
		if (same_task(cs->arr[x],ps->arr[y]))
			stats_diff(cs->arr[x],ps->arr[y],time_s,ts_c);
		else // the old task is left in ps and freed with it
			stats_new(cs->arr[x],ts_c);
		update_maxpidlen(cs->arr[x]->tid);
		x++;
		y++;
	}

	if (exn&&arr_merge(cs,ex,exn)) // the moved items are in tid order and none of them is in cs
		for (n=0;n<exn;n++) // free the data in case merge fails
			free_stats(ex[n]);
	if (ex)
		free(ex);

	// reattach exited threads back to their original process; adding them one by one into a long thread list
	// would move its tail each time, so merge all of them of a process at once
	for (n=0;cs->arr&&n<cs->length;n++) {
		struct xxxid_stats *c=cs->arr[n];

		if (c->pid!=c->tid&&c->exited) {
			if (!et) // at most the remaining items of cs are exited threads
				et=malloc((cs->length-n)*sizeof *et);
			if (!et) // ignore the old data in case of memory alloc error
				break;
			et[etn++]=c;
		}
	}
	if (etn>1)
		qsort(et,etn,sizeof *et,exited_thread_cmp);
	for (n=0;n<etn;n=e) {
		struct xxxid_stats *p;

		for (e=n+1;e<etn&&et[e]->pid==et[n]->pid;e++)
			;
		p=arr_find(cs,et[n]->pid); // pid==tid for the main process
		if (!p)
			continue;
		if (!p->threads)
			p->threads=arr_alloc();
		if (!p->threads) // ignore the old data in case of memory alloc error
			continue;
		arr_merge(p->threads,et+n,e-n); // p has only its live threads, none of these
	}
	if (et)
		free(et);

	return cs->length;
}

// count the lines that the view shows - the processes and their threads that pass the filter
inline int count_visible(struct xxxid_stats_arr *cs,filter_callback_w cb,int width) {
	int cnt=0;
	int n,k;

	for (n=0;cs->arr&&n<cs->length;n++) {
		struct xxxid_stats *c=cs->arr[n];

		if (c->pid!=c->tid&&!config.f.flat) // threads are shown only below their process, unless listed alone
			continue;
		if (!cb(c,width))
			cnt++;
		if (config.f.processes||config.f.flat)
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

// the value that a column shows in the accumulated modes: the total since the task was first seen,
// or with accum-bw the average bandwidth over that time
inline double accum_val(const struct xxxid_stats *s,e_accum v,int agg) {
	double acc=0;

	switch (v) {
		case E_ACC_READ:
			acc=agg?s->read_val_acc_p:s->read_val_acc;
			break;
		case E_ACC_WRITE:
			acc=agg?s->write_val_acc_p:s->write_val_acc;
			break;
		case E_ACC_VFS_READ:
			acc=agg?s->read_char_val_acc_p:s->read_char_val_acc;
			break;
		case E_ACC_VFS_WRITE:
			acc=agg?s->write_char_val_acc_p:s->write_char_val_acc;
			break;
	}
	if (config.f.accumbw)
		return acc/timediff_in_s(s->ts_s,s->ts_e);
	return acc;
}

static int grlen=0; // count of the newest history values that the graph shows

// the curses view keeps the sorted lists while struct view_key is unchanged, so any input of the sort
// besides the compared items has to be there too; agg compares the values of the whole processes
static inline int sort_cmp(const void *a,const void *b,int agg) {
	int order=config.f.sort_order?1:-1; // SORT_ASC is bit 0=1, else should reverse sort
	struct xxxid_stats **ppa=(struct xxxid_stats **)a;
	struct xxxid_stats **ppb=(struct xxxid_stats **)b;
	struct xxxid_stats *pa,*pb;
	int res=0;

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
					aa=HIST_SUM8(pa->hist,io,agg,grlen);
					ab=HIST_SUM8(pb->hist,io,agg,grlen);
					res=aa-ab;
					break;
				case E_GR_R:
					if (grlen==0)
						grlen=HISTORY_CNT;
					if (config.f.accumulated||config.f.accumbw) {
						da=accum_val(pa,E_ACC_READ,agg);
						db=accum_val(pb,E_ACC_READ,agg);
					} else {
						da=HIST_SUM(pa->hist,read,agg,grlen);
						db=HIST_SUM(pb->hist,read,agg,grlen);
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
					if (config.f.accumulated||config.f.accumbw) {
						da=accum_val(pa,E_ACC_WRITE,agg);
						db=accum_val(pb,E_ACC_WRITE,agg);
					} else {
						da=HIST_SUM(pa->hist,write,agg,grlen);
						db=HIST_SUM(pb->hist,write,agg,grlen);
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
					if (config.f.accumulated||config.f.accumbw) {
						da=accum_val(pa,E_ACC_READ,agg)+accum_val(pa,E_ACC_WRITE,agg);
						db=accum_val(pb,E_ACC_READ,agg)+accum_val(pb,E_ACC_WRITE,agg);
					} else {
						da=HIST_SUM(pa->hist,read,agg,grlen)+HIST_SUM(pa->hist,write,agg,grlen);
						db=HIST_SUM(pb->hist,read,agg,grlen)+HIST_SUM(pb->hist,write,agg,grlen);
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
					aa=HIST_SUM8(pa->hist,si,agg,grlen);
					ab=HIST_SUM8(pb->hist,si,agg,grlen);
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
			if (config.f.accumulated||config.f.accumbw)
				res=accum_val(pa,E_ACC_READ,agg)>accum_val(pb,E_ACC_READ,agg)?1:
					accum_val(pa,E_ACC_READ,agg)<accum_val(pb,E_ACC_READ,agg)?-1:0;
			else
				res=(agg?pa->read_val_p:pa->read_val)>(agg?pb->read_val_p:pb->read_val)?1:
					(agg?pa->read_val_p:pa->read_val)<(agg?pb->read_val_p:pb->read_val)?-1:0;
			break;
		case SORT_BY_WRITE:
			if (config.f.accumulated||config.f.accumbw)
				res=accum_val(pa,E_ACC_WRITE,agg)>accum_val(pb,E_ACC_WRITE,agg)?1:
					accum_val(pa,E_ACC_WRITE,agg)<accum_val(pb,E_ACC_WRITE,agg)?-1:0;
			else
				res=(agg?pa->write_val_p:pa->write_val)>(agg?pb->write_val_p:pb->write_val)?1:
					(agg?pa->write_val_p:pa->write_val)<(agg?pb->write_val_p:pb->write_val)?-1:0;
			break;
		case SORT_BY_VFS_READ:
			if (config.f.accumulated||config.f.accumbw)
				res=accum_val(pa,E_ACC_VFS_READ,agg)>accum_val(pb,E_ACC_VFS_READ,agg)?1:
					accum_val(pa,E_ACC_VFS_READ,agg)<accum_val(pb,E_ACC_VFS_READ,agg)?-1:0;
			else
				res=(agg?pa->read_char_val_p:pa->read_char_val)>(agg?pb->read_char_val_p:pb->read_char_val)?1:
					(agg?pa->read_char_val_p:pa->read_char_val)<(agg?pb->read_char_val_p:pb->read_char_val)?-1:0;
			break;
		case SORT_BY_VFS_WRITE:
			if (config.f.accumulated||config.f.accumbw)
				res=accum_val(pa,E_ACC_VFS_WRITE,agg)>accum_val(pb,E_ACC_VFS_WRITE,agg)?1:
					accum_val(pa,E_ACC_VFS_WRITE,agg)<accum_val(pb,E_ACC_VFS_WRITE,agg)?-1:0;
			else
				res=(agg?pa->write_char_val_p:pa->write_char_val)>(agg?pb->write_char_val_p:pb->write_char_val)?1:
					(agg?pa->write_char_val_p:pa->write_char_val)<(agg?pb->write_char_val_p:pb->write_char_val)?-1:0;
			break;
		case SORT_BY_SWAPIN:
			res=(agg?pa->swapin_val_p:pa->swapin_val)>(agg?pb->swapin_val_p:pb->swapin_val)?1:
				(agg?pa->swapin_val_p:pa->swapin_val)<(agg?pb->swapin_val_p:pb->swapin_val)?-1:0;
			break;
		case SORT_BY_IO:
			res=(agg?pa->blkio_val_p:pa->blkio_val)>(agg?pb->blkio_val_p:pb->blkio_val)?1:
				(agg?pa->blkio_val_p:pa->blkio_val)<(agg?pb->blkio_val_p:pb->blkio_val)?-1:0;
			break;
	}
	res*=order;
	return res;
}

// compare the tasks by the values that the view shows; a NULL a sets the graph width from b
inline int iotop_sort_cb(const void *a,const void *b) {
	if (!a) {
		grlen=(long)b;
		return 0;
	}
	return sort_cmp(a,b,config.f.processes);
}

// compare the processes by the values of the whole process, also when their threads are shown
inline int iotop_sort_proc_cb(const void *a,const void *b) {
	return sort_cmp(a,b,1);
}


// any input of the filter besides s has to be in struct view_key, the curses view relies on it
inline int filter1(struct xxxid_stats *s) {
	if (!s->cmdline_long||!s->cmdline_short) // if strdup fails during copy, those may become NULL
		return 1;

	if ((params.user_id!=-1)&&(s->euid!=(uid_t)params.user_id))
		return 1;

	if ((params.pid!=-1)&&(s->tid!=params.pid))
		return 1;

	return 0;
}
