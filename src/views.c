/* SPDX-License-Identifier: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020-2026  Boian Bonev

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

#include "iotop.h"

#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int64_t wall_off; // the wall clock minus the monotonic clock in milliseconds, for the start times of the tasks

// the bytes of all tasks in the cycle, or accumulated in the accumulated mode
inline void calc_total(struct xxxid_stats_arr *cs,uint64_t *read,uint64_t *write) {
	int i;

	*read=*write=0;

	for (i=0;i<cs->length;i++) {
		if (!config.f.accumulated) {
			*read+=cs->arr[i]->read_diff;
			*write+=cs->arr[i]->write_diff;
		} else {
			*read+=accum_bytes(cs->arr[i],E_ACC_READ,0,!config.f.nolifetime);
			*write+=accum_bytes(cs->arr[i],E_ACC_WRITE,0,!config.f.nolifetime);
		}
	}
}

// growth of a counter between two cycles; the 64-bit counters do not wrap, a decrease is not growth:
// the sums of a process lose the counters of its exited threads, and a new task may get the tid of an old one
inline uint64_t rrv(uint64_t to,uint64_t from) {
	return to<from?0:to-from;
}

// the bytes that the block devices read and wrote in the cycle
inline void calc_a_total(struct act_stats *act,uint64_t *read,uint64_t *write) {
	*read=*write=0;

	if (act->have_o) {
		*read=rrv(act->read_bytes,act->read_bytes_o);
		*write=rrv(act->write_bytes,act->write_bytes_o);
	}
}

// the percentage of the time of a cycle that a task waited, from its delay in nanoseconds
inline double delay_pc(uint64_t ns,double time_s) {
	double pc=(double)ns/(time_s*10000000.0);

	return pc>100?100:pc;
}

// the graph level of val relative to mx: over 75%, 50%, 25% or above zero
inline int value2scale(uint64_t val,uint64_t mx) {
	if (val*4>mx*3)
		return 4;
	if (val*2>mx)
		return 3;
	if (val*4>mx)
		return 2;
	if (val)
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
inline uint64_t hist_sum(const struct xxxid_hist *h,const uint64_t *a,int cnt) {
	uint64_t sum=0;
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
static inline void hist_put(struct xxxid_hist *h,uint64_t *a,uint64_t *sum,uint64_t v) {
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
	c->blkio_diff=0;
	c->swapin_diff=0;
	c->read_diff=0;
	c->write_diff=0;
	c->read_char_diff=0;
	c->write_char_diff=0;
	c->read_val_acc=0;
	c->write_val_acc=0;
	c->read_char_val_acc=0;
	c->write_char_val_acc=0;
	c->ts_s=ts_c; // keep start ts
	c->ts_e=ts_c; // keep end ts
}

// calculate the values of the current cycle from the previous data in p
static inline void stats_diff(struct xxxid_stats *c,struct xxxid_stats *p,uint64_t cyc_ns,uint64_t ts_c) {
	uint64_t rv,wv,rcv,wcv,rvp=0,wvp=0;
	int io,si,iop=0,sip=0;
	struct xxxid_hist *h;

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
	c->blkio_diff=rrv(c->blkio_delay_total,p->blkio_delay_total);
	c->swapin_diff=rrv(c->swapin_delay_total,p->swapin_delay_total);

	rv=rrv(c->read_bytes,p->read_bytes);
	wv=rrv(c->write_bytes,p->write_bytes);
	rcv=rrv(c->read_char,p->read_char);
	wcv=rrv(c->write_char,p->write_char);

	c->read_diff=rv;
	c->write_diff=wv;
	c->read_char_diff=rcv;
	c->write_char_diff=wcv;

	c->read_val_acc=p->read_val_acc+rv;
	c->write_val_acc=p->write_val_acc+wv;
	c->read_char_val_acc=p->read_char_val_acc+rcv;
	c->write_char_val_acc=p->write_char_val_acc+wcv;

	io=value2scale(c->blkio_diff,cyc_ns);
	si=value2scale(c->swapin_diff,cyc_ns);

	if (c->pid==c->tid) {
		c->blkio_diff_p=rrv(c->blkio_delay_total_p,p->blkio_delay_total_p);
		c->swapin_diff_p=rrv(c->swapin_delay_total_p,p->swapin_delay_total_p);

		rvp=rrv(c->read_bytes_p,p->read_bytes_p);
		wvp=rrv(c->write_bytes_p,p->write_bytes_p);
		rcv=rrv(c->read_char_p,p->read_char_p);
		wcv=rrv(c->write_char_p,p->write_char_p);

		c->read_diff_p=rvp;
		c->write_diff_p=wvp;
		c->read_char_diff_p=rcv;
		c->write_char_diff_p=wcv;

		c->read_val_acc_p=p->read_val_acc_p+rvp;
		c->write_val_acc_p=p->write_val_acc_p+wvp;
		c->read_char_val_acc_p=p->read_char_val_acc_p+rcv;
		c->write_char_val_acc_p=p->write_char_val_acc_p+wcv;

		iop=value2scale(c->blkio_diff_p,cyc_ns);
		sip=value2scale(c->swapin_diff_p,cyc_ns);
	}

	// an idle task keeps the shared empty history; the older values of a history are all zero,
	// so allocating it with the first nonzero value gives the same result as storing zeros until then
	if (h==&hist_none&&!(io||si||rv||wv||iop||sip||rvp||wvp))
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
	if (h->proc==&hist_p_none&&!(iop||sip||rvp||wvp)) // the same for the aggregated history
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
	o->blkio_diff=0;
	o->swapin_diff=0;
	o->read_diff=0;
	o->write_diff=0;
	o->read_char_diff=0;
	o->write_char_diff=0;
	if (h) { // store zero as the newest values; without own history it is all zero anyway
		h->pos=(h->pos+1)%HISTORY_CNT;
		if (h->cnt<HISTORY_CNT)
			h->cnt++;
		hist_put8(h,h->iohist,&h->iosum,0);
		hist_put8(h,h->sihist,&h->sisum,0);
		hist_put(h,h->readhist,&h->readsum,0);
		hist_put(h,h->writehist,&h->writesum,0);
		if (o->tid==o->pid&&h->proc!=&hist_p_none) { // process aggregated data, only for main process
			struct xxxid_hist_p *hp=h->proc;

			hist_put8(h,hp->iohist_p,&hp->iosum_p,0);
			hist_put8(h,hp->sihist_p,&hp->sisum_p,0);
			hist_put(h,hp->readhist_p,&hp->readsum_p,0);
			hist_put(h,hp->writehist_p,&hp->writesum_p,0);
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
inline int create_diff(struct xxxid_stats_arr *cs,struct xxxid_stats_arr *ps,uint64_t cyc_ms,uint64_t ts_c,int keep_exited) {
	struct xxxid_stats **ex=NULL; // the processes and tasks that exited, moved from ps
	struct xxxid_stats **et=NULL; // the exited threads in cs
	struct timespec rt;
	int exn=0,etn=0;
	int ncs,nps;
	int n,x,y,e;

	if (cs->diffed) // the data of this cycle is already done; redraws do not need it again
		return cs->length;
	cs->diffed=1;

	clock_gettime(CLOCK_REALTIME,&rt);
	wall_off=(int64_t)rt.tv_sec*1000+rt.tv_nsec/1000000-monotime();

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
			stats_diff(cs->arr[x],ps->arr[y],cyc_ms*1000000,ts_c);
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

// the bytes of v that a task did since it started if life is set, else since it was first seen
inline uint64_t accum_bytes(const struct xxxid_stats *s,e_accum v,int agg,int life) {
	uint64_t cnt=0; // the counter of the kernel, from the start of the task
	uint64_t acc=0;

	switch (v) {
		case E_ACC_READ:
			acc=agg?s->read_val_acc_p:s->read_val_acc;
			cnt=agg?s->read_bytes_p:s->read_bytes;
			break;
		case E_ACC_WRITE:
			acc=agg?s->write_val_acc_p:s->write_val_acc;
			cnt=agg?s->write_bytes_p:s->write_bytes;
			break;
		case E_ACC_VFS_READ:
			acc=agg?s->read_char_val_acc_p:s->read_char_val_acc;
			cnt=agg?s->read_char_p:s->read_char;
			break;
		case E_ACC_VFS_WRITE:
			acc=agg?s->write_char_val_acc_p:s->write_char_val_acc;
			cnt=agg?s->write_char_p:s->write_char;
			break;
	}
	return life?cnt:acc;
}

// the bytes that the value of a column in the accumulated modes shows: the total since the task started,
// or with --no-lifetime since it was first seen; age is the time in milliseconds that accum-bw averages over
inline uint64_t accum_src(const struct xxxid_stats *s,e_accum v,int agg,uint64_t *age) {
	*age=timediff_in_ms(s->ts_s,s->ts_e);
	if (!config.f.accumbw)
		return accum_bytes(s,v,agg,!config.f.nolifetime);
	if (!config.f.nolifetime&&s->btime) { // the start time is known since taskstats v12, else average since first seen
		int64_t life=(int64_t)s->ts_e-((int64_t)s->btime*1000-wall_off);

		if (life>(int64_t)*age)
			*age=(uint64_t)life;
		return accum_bytes(s,v,agg,1);
	}
	return accum_bytes(s,v,agg,0);
}

// the value that a column shows in the accumulated modes, with accum-bw the average bandwidth
inline double accum_val(const struct xxxid_stats *s,e_accum v,int agg) {
	uint64_t age;
	uint64_t b=accum_src(s,v,agg,&age);

	if (config.f.accumbw)
		return (double)b*1000.0/(double)age;
	return (double)b;
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
			uint64_t ua=0,ub=0;
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
					ua=HIST_SUM(pa->hist,read,agg,grlen);
					ub=HIST_SUM(pb->hist,read,agg,grlen);
					if (ua>ub)
						res=1;
					else if (ua<ub)
						res=-1;
					else
						res=0;
					break;
				case E_GR_W:
					if (grlen==0)
						grlen=HISTORY_CNT;
					ua=HIST_SUM(pa->hist,write,agg,grlen);
					ub=HIST_SUM(pb->hist,write,agg,grlen);
					if (ua>ub)
						res=1;
					else if (ua<ub)
						res=-1;
					else
						res=0;
					break;
				case E_GR_RW:
					if (grlen==0)
						grlen=HISTORY_CNT;
					ua=HIST_SUM(pa->hist,read,agg,grlen)+HIST_SUM(pa->hist,write,agg,grlen);
					ub=HIST_SUM(pb->hist,read,agg,grlen)+HIST_SUM(pb->hist,write,agg,grlen);
					if (ua>ub)
						res=1;
					else if (ua<ub)
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
			res=(agg?pa->read_diff_p:pa->read_diff)>(agg?pb->read_diff_p:pb->read_diff)?1:
				(agg?pa->read_diff_p:pa->read_diff)<(agg?pb->read_diff_p:pb->read_diff)?-1:0;
			break;
		case SORT_BY_WRITE:
			res=(agg?pa->write_diff_p:pa->write_diff)>(agg?pb->write_diff_p:pb->write_diff)?1:
				(agg?pa->write_diff_p:pa->write_diff)<(agg?pb->write_diff_p:pb->write_diff)?-1:0;
			break;
		case SORT_BY_VFS_READ:
			res=(agg?pa->read_char_diff_p:pa->read_char_diff)>(agg?pb->read_char_diff_p:pb->read_char_diff)?1:
				(agg?pa->read_char_diff_p:pa->read_char_diff)<(agg?pb->read_char_diff_p:pb->read_char_diff)?-1:0;
			break;
		case SORT_BY_VFS_WRITE:
			res=(agg?pa->write_char_diff_p:pa->write_char_diff)>(agg?pb->write_char_diff_p:pb->write_char_diff)?1:
				(agg?pa->write_char_diff_p:pa->write_char_diff)<(agg?pb->write_char_diff_p:pb->write_char_diff)?-1:0;
			break;
		case SORT_BY_SWAPIN:
			res=(agg?pa->swapin_diff_p:pa->swapin_diff)>(agg?pb->swapin_diff_p:pb->swapin_diff)?1:
				(agg?pa->swapin_diff_p:pa->swapin_diff)<(agg?pb->swapin_diff_p:pb->swapin_diff)?-1:0;
			break;
		case SORT_BY_IO:
			res=(agg?pa->blkio_diff_p:pa->blkio_diff)>(agg?pb->blkio_diff_p:pb->blkio_diff)?1:
				(agg?pa->blkio_diff_p:pa->blkio_diff)<(agg?pb->blkio_diff_p:pb->blkio_diff)?-1:0;
			break;
	}
	res*=order;
	return res;
}

static int sort_agg; // the sort compares the values of the whole processes

// compare the tasks by the values that the view shows; a NULL a sets the graph width from b
inline int iotop_sort_cb(const void *a,const void *b) {
	if (!a) {
		grlen=(long)b;
		return 0;
	}
	return sort_cmp(a,b,sort_agg);
}

// the values of the accumulated modes that the sort compares as bits of e_accum, 0 when it does not
static inline int accum_sort_mask(void) {
	if (!config.f.accumulated&&!config.f.accumbw)
		return 0;
	switch (masked_sort_by(0)) {
		case SORT_BY_GRAPH:
			switch (masked_grtype(0)) {
				case E_GR_R:
					return 1<<E_ACC_READ;
				case E_GR_W:
					return 1<<E_ACC_WRITE;
				case E_GR_RW:
					return 1<<E_ACC_READ|1<<E_ACC_WRITE;
				default:
					return 0;
			}
		case SORT_BY_READ:
			return 1<<E_ACC_READ;
		case SORT_BY_WRITE:
			return 1<<E_ACC_WRITE;
		case SORT_BY_VFS_READ:
			return 1<<E_ACC_VFS_READ;
		case SORT_BY_VFS_WRITE:
			return 1<<E_ACC_VFS_WRITE;
		default:
			return 0;
	}
}

// the sort key of s in the accumulated modes: the sum of the values in mask, with accum-bw in thousandths of
// bytes per second, fine enough to order the averages that the view shows with two decimals
static inline uint64_t accum_key(const struct xxxid_stats *s,int mask,int agg) {
	uint64_t age=1,b=0;
	int v;

	for (v=E_ACC_READ;v<=E_ACC_VFS_WRITE;v++)
		if (mask&1<<v)
			b+=accum_src(s,(e_accum)v,agg,&age);
	if (!config.f.accumbw)
		return b;
	return b/age*1000000+b%age*1000000/age;
}

struct sort_item {
	uint64_t key;
	struct xxxid_stats *s;
};

static int sort_item_cb(const void *a,const void *b) {
	const struct sort_item *ka=a;
	const struct sort_item *kb=b;
	int order=config.f.sort_order?1:-1; // SORT_ASC is bit 0=1, else should reverse sort

	return (ka->key>kb->key?1:ka->key<kb->key?-1:0)*order;
}

// sort the items of a for which keep returns nonzero, all of them if keep is NULL; agg compares the values of
// the whole processes; the accumulated values are computed once for each item, not for each comparison
inline void iotop_sort(struct xxxid_stats_arr *a,int agg,int (*keep)(const struct xxxid_stats *s)) {
	int mask=accum_sort_mask();
	struct sort_item *k;
	int i;

	sort_agg=agg;
	if (!mask) {
		arr_sort(a,iotop_sort_cb,keep);
		return;
	}
	arr_sort(a,NULL,keep);
	if (!a||!a->sor||!a->sor_length)
		return;
	k=malloc(a->sor_length*sizeof *k);
	if (!k) // the items stay unsorted
		return;
	for (i=0;i<a->sor_length;i++) {
		k[i].key=accum_key(a->sor[i],mask,agg);
		k[i].s=a->sor[i];
	}
	qsort(k,a->sor_length,sizeof *k,sort_item_cb);
	for (i=0;i<a->sor_length;i++)
		a->sor[i]=k[i].s;
	free(k);
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
