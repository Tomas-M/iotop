/* SPDX-License-Identifier: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020-2026  Boian Bonev

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

#include "iotop.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

inline struct xxxid_stats_arr *arr_alloc(void) {
	struct xxxid_stats_arr *a;

	a=calloc(1,sizeof *a);

	if (!a)
		return NULL;

	a->arr=calloc(PROC_LIST_SZ_INI,sizeof *a->arr);
	if (!a->arr) {
		free(a);
		return NULL;
	}
	a->size=PROC_LIST_SZ_INI;
	return a;
}

static inline int arr_resize(struct xxxid_stats_arr *a,int newsize) {
	struct xxxid_stats **t;

	if (!a)
		return EINVAL;
	if (a->size>=newsize)
		return 0;

	newsize=(newsize+PROC_LIST_SZ_INC-1)/PROC_LIST_SZ_INC;
	newsize*=PROC_LIST_SZ_INC;
	t=realloc(a->arr,newsize*sizeof(struct xxxid_stats *));
	if (!t)
		return ENOMEM;

	a->arr=t;
	a->size=newsize;

	return 0;
}

inline int arr_add(struct xxxid_stats_arr *pa,struct xxxid_stats *ps) {
	int a=-1;
	int i,s,e;
	pid_t r;
	int res;

	if (!pa)
		return EINVAL;
	if (!ps)
		return EINVAL;

	res=arr_resize(pa,pa->length+1);
	if (res)
		return res;

	if (pa->sor) {
		free(pa->sor);
		pa->sor=NULL;
	}

	s=0;
	e=pa->length;
	for (;;) {
		if (e-s<5) {
			for (i=s;i<e;i++) {
				r=ps->tid-pa->arr[i]->tid;
				if (!r)
					return EINVAL; // can't add duplicate
				if (r<0)
					break;
			}
			a=i;
			break;
		} else {
			i=s+(e-s)/2;
			r=ps->tid-pa->arr[i]->tid;
			if (!r)
				return EINVAL; // can't add duplicate
			if (r<0)
				e=i;
			else
				s=i+1;
		}
	}

	// add at position a
	if (a!=pa->length)
		memmove(pa->arr+a+1,pa->arr+a,(pa->length-a)*sizeof *pa->arr);
	pa->arr[a]=ps;
	pa->length++;

	return 0; // SUCCESS
}

// add an item at the end without keeping the order; arr_finalize has to be called before any other use
inline int arr_append(struct xxxid_stats_arr *pa,struct xxxid_stats *ps) {
	int res;

	if (!pa)
		return EINVAL;
	if (!ps)
		return EINVAL;

	res=arr_resize(pa,pa->length+1);
	if (res)
		return res;

	pa->arr[pa->length++]=ps;

	return 0; // SUCCESS
}

static int arr_cmp_tid(const void *a,const void *b) {
	pid_t ta=(*(struct xxxid_stats **)a)->tid;
	pid_t tb=(*(struct xxxid_stats **)b)->tid;

	return ta<tb?-1:ta>tb;
}

// sort the items added by arr_append by tid; duplicates are dropped and freed
inline void arr_finalize(struct xxxid_stats_arr *pa) {
	int i,j;

	if (!pa)
		return;
	if (pa->length<2)
		return;

	if (pa->sor) {
		free(pa->sor);
		pa->sor=NULL;
	}

	qsort(pa->arr,pa->length,sizeof *pa->arr,arr_cmp_tid);
	for (i=1,j=1;i<pa->length;i++)
		if (pa->arr[i]->tid==pa->arr[j-1]->tid)
			free_stats(pa->arr[i]);
		else
			pa->arr[j++]=pa->arr[i];
	pa->length=j;
}

// merge cnt items, sorted by tid, into pa; none of them may be already present there
inline int arr_merge(struct xxxid_stats_arr *pa,struct xxxid_stats **items,int cnt) {
	int i,j,k,res;

	if (!pa)
		return EINVAL;
	if (!items)
		return EINVAL;
	if (cnt<1)
		return 0;

	res=arr_resize(pa,pa->length+cnt);
	if (res)
		return res;

	if (pa->sor) {
		free(pa->sor);
		pa->sor=NULL;
	}

	// merge backwards, the items left in place at the end are already in order
	i=pa->length-1;
	j=cnt-1;
	k=pa->length+cnt-1;
	while (j>=0)
		if (i>=0&&pa->arr[i]->tid>items[j]->tid)
			pa->arr[k--]=pa->arr[i--];
		else
			pa->arr[k--]=items[j--];
	pa->length+=cnt;

	return 0; // SUCCESS
}

inline struct xxxid_stats *arr_find(struct xxxid_stats_arr *pa,pid_t tid) {
	int i,s,e,r;

	if (!pa)
		return NULL;

	s=0;
	e=pa->length;
	for (;;) {
		if (e-s<5) {
			for (i=s;i<e;i++) {
				r=tid-pa->arr[i]->tid;
				if (!r)
					return pa->arr[i];
				if (r<0)
					break;
			}
			return NULL;
		} else {
			i=s+(e-s)/2;
			r=tid-pa->arr[i]->tid;
			if (!r)
				return pa->arr[i];
			if (r<0)
				e=i;
			else
				s=i+1;
		}
	}
}

static inline void _arr_free(struct xxxid_stats_arr *pa,int freeitem) {
	if (!pa)
		return;
	if (pa->arr) {
		if (freeitem) {
			int i;

			for (i=0;i<pa->length;i++)
				free_stats(pa->arr[i]);
		}
		free(pa->arr);
	}
	if (pa->sor)
		free(pa->sor);
	free(pa);
}

inline void arr_free(struct xxxid_stats_arr *pa) {
	_arr_free(pa,1);
}

inline void arr_free_noitem(struct xxxid_stats_arr *pa) {
	_arr_free(pa,0);
}

inline void arr_sort(struct xxxid_stats_arr *pa,int (*cb)(const void *a,const void *b)) {
	if (!pa)
		return;
	if (pa->sor)
		free(pa->sor);
	pa->sor=NULL;
	if (!pa->length)
		return;
	pa->sor=calloc(pa->length,sizeof *pa->arr);
	if (!pa->sor)
		return;

	memcpy(pa->sor,pa->arr,pa->length*sizeof *pa->arr);
	qsort(pa->sor,pa->length,sizeof *pa->sor,cb);
}

