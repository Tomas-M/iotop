/* SPDX-License-Identifier: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020-2026  Boian Bonev

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/

#ifndef __IOTOP_H__
#define __IOTOP_H__

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <regex.h>
#include <stdint.h>
#include <sys/types.h>

#include "ucell.h"

#define VERSION "1.32"

typedef enum {
	E_GR_IO,
	E_GR_R,
	E_GR_W,
	E_GR_RW,
	E_GR_SW,
	E_GR_MIN=E_GR_IO,
	E_GR_MAX=E_GR_SW,
} e_grtype;

typedef enum {
	E_UID2NAME_ALWAYS,	/* Always resolve uid with getpwuid(3) */
	E_UID2NAME_NONE,	/* Print uid as user name */
	E_UID2NAME_CYCLIC,	/* Cache resolved uid within each refresh cycle */
	E_UID2NAME_STATIC,	/* Cache resolved uid permanently across refresh cycles */
} e_uid2name;

typedef enum { // the values of the accumulated modes
	E_ACC_READ,
	E_ACC_WRITE,
	E_ACC_VFS_READ,
	E_ACC_VFS_WRITE,
} e_accum;

typedef union {
	struct _flags {
		int batch_mode;
		int only;
		int processes;
		int accumulated;
		int kilobytes;
		int timestamp;
		int quiet;
		int fullcmdline;
		int hidepid;
		int hideprio;
		int hideuser;
		int hideread;
		int hidevfsread;
		int hidewrite;
		int hidevfswrite;
		int hideswapin;
		int hideio;
		int hidegraph;
		int hidecmd;
		int deadx;
		int hideexited;
		int nocolor;
		int reverse_graph;
		int hideclock;
		int accumbw;
		int inverse;
		int unicode; // this and below are not part of opts
		e_grtype grtype;
		int helptype;
		int sort_by;
		int sort_order;
		int base; // 1000 or 1024
		int threshold; // 1..10
		int norenice;
		e_uid2name uid2name;
		int flat; // list the threads alone, not below their process
		int nolifetime; // the accumulated modes count since the task was first seen, not since it started
	} f;
	int opts[26];
} config_t;

typedef struct {
	int iter;
	int delay_ms; // delay between iterations in milliseconds
	int pid;
	int user_id;
	char *search_str; // search regex string
	regex_t search_regx; // search regex
	int search_regx_ok; // search regex compiles ok
	ucell *search_uc; // utf cell array
} params_t;

extern config_t config;
extern params_t params;
extern int maxpidlen;
extern unsigned taskstats_ver;
extern struct xxxid_hist hist_none;
extern struct xxxid_hist_p hist_p_none;
extern int hist_w;

#define IOTOP_TASKSTATS_MINVER 4U
#define IOTOP_TASKSTATS_VERSION 15

#define HISTORY_POS 60
#define HISTORY_CNT (HISTORY_POS*2)

struct xxxid_stats_arr {
	struct xxxid_stats **arr;
	struct xxxid_stats **sor; // the items that arr_sort kept, sorted
	int sor_length;
	int length;
	int size;
	int diffed; // create_diff was already applied
	unsigned sort_gen; // the curses view generation that sor was made for, 0 for none
};

// history aggregated in main process; it uses pos and cnt of the struct xxxid_hist it belongs to
struct xxxid_hist_p {
	// sums of the newest hist_w values of each array
	int iosum_p;
	int sisum_p;
	uint64_t readsum_p;
	uint64_t writesum_p;
	uint8_t iohist_p[HISTORY_CNT]; // io history data (aggregated in main process)
	uint8_t sihist_p[HISTORY_CNT]; // swapin history data (aggregated in main process)
	uint64_t readhist_p[HISTORY_CNT]; // bytes read in each cycle (aggregated in main process)
	uint64_t writehist_p[HISTORY_CNT]; // bytes written in each cycle (aggregated in main process)
};

// history of the GRAPH column; it is handed over to the item of the next cycle instead of being copied
// each array is a ring buffer with the newest value at pos
struct xxxid_hist {
	int pos; // index of the newest value
	int cnt; // count of the stored values; the older ones are all zero
	// sums of the newest hist_w values of each array
	int iosum;
	int sisum;
	uint64_t readsum;
	uint64_t writesum;
	uint8_t iohist[HISTORY_CNT]; // io history data
	uint8_t sihist[HISTORY_CNT]; // swapin history data
	uint64_t readhist[HISTORY_CNT]; // bytes read in each cycle
	uint64_t writehist[HISTORY_CNT]; // bytes written in each cycle
	struct xxxid_hist_p *proc; // never NULL, only main processes get own, the rest point to hist_p_none
};

// index of the k-th newest value of history h; k=0 is the newest, k<HISTORY_CNT
#define HIST_IDX(h,k) (((h)->pos+HISTORY_CNT-(k))%HISTORY_CNT)
// the k-th newest value of history array a of item s
#define HISTV(s,a,k) ((s)->hist->a[HIST_IDX((s)->hist,(k))])
// the k-th newest value of aggregated history array a of item s
#define HISTV_P(s,a,k) ((s)->hist->proc->a[HIST_IDX((s)->hist,(k))])
// sum of the newest cnt values of history a (io, si, read or write) of h, the aggregated one if agg is set
#define HIST_SUM(h,a,agg,cnt) ((cnt)==hist_w?((agg)?(h)->proc->a##sum_p:(h)->a##sum):hist_sum((h),(agg)?(h)->proc->a##hist_p:(h)->a##hist,(cnt)))
#define HIST_SUM8(h,a,agg,cnt) ((cnt)==hist_w?((agg)?(h)->proc->a##sum_p:(h)->a##sum):hist_sum8((h),(agg)?(h)->proc->a##hist_p:(h)->a##hist,(cnt)))

struct xxxid_stats {
	pid_t pid;
	pid_t tid;
	uint64_t swapin_delay_total; // nanoseconds
	uint64_t blkio_delay_total; // nanoseconds
	uint64_t read_bytes;
	uint64_t write_bytes;
	uint64_t read_char;
	uint64_t write_char;
	uint64_t swapin_delay_total_p; // aggregated data from all threads
	uint64_t blkio_delay_total_p; // used for process view
	uint64_t read_bytes_p;
	uint64_t write_bytes_p;
	uint64_t read_char_p;
	uint64_t write_char_p;
	uint64_t ts_s; // start timestamp for accum-bw
	uint64_t ts_e; // end timestamp for accum-bw
	uint64_t btime; // start time and exe of the task from taskstats, 0 if not known;
	uint64_t exe; // they tell that a task of the next cycle is the same process

	uint64_t blkio_diff; // nanoseconds waited in the last cycle
	uint64_t swapin_diff;
	uint64_t read_diff; // bytes in the last cycle
	uint64_t write_diff;
	uint64_t read_char_diff;
	uint64_t write_char_diff;
	uint64_t read_val_acc; // bytes since the task was first seen
	uint64_t write_val_acc;
	uint64_t read_char_val_acc;
	uint64_t write_char_val_acc;

	uint64_t blkio_diff_p;
	uint64_t swapin_diff_p;
	uint64_t read_diff_p;
	uint64_t write_diff_p;
	uint64_t read_char_diff_p;
	uint64_t write_char_diff_p;
	uint64_t read_val_acc_p;
	uint64_t write_val_acc_p;
	uint64_t read_char_val_acc_p;
	uint64_t write_char_val_acc_p;

	int io_prio;

	uid_t euid;
	char *cmdline_long;
	char *cmdline_short;
	char *cmdline_comm;
	const char *pw_name; // interned or static, never freed

	struct xxxid_hist *hist; // never NULL, items without own history point to hist_none

	int exited; // exited>0 shows for how many refresh cycles the process is gone
	int error_x; // netlink api did not return valid data
	int error_i; // get_ioprio did not return valid data
	// there is no point to keep in memory data for processes exited before HISTORY_CNT cycles
	struct xxxid_stats_arr *threads;
};

// arrays are used both for main process/thread list and for inner thread list belonging to a process
// always start small, afterwards double the size
#define PROC_LIST_SZ_INI 16

struct act_stats {
	uint64_t read_bytes;
	uint64_t write_bytes;
	uint64_t read_bytes_o;
	uint64_t write_bytes_o;
	uint64_t ts_c;
	uint64_t ts_o;
	uint8_t have_o;
};

inline void nl_init(void);
inline void nl_fini(void);

inline int nl_xxxid_info(pid_t tid,pid_t pid,struct xxxid_stats *stats,char *comm);

typedef int (*filter_callback)(struct xxxid_stats *);
typedef int (*filter_callback_w)(struct xxxid_stats *,int width);

inline struct xxxid_stats_arr *fetch_data(filter_callback filter,struct xxxid_stats_arr *prev);
void cmdline_reread(void);
inline void free_stats(struct xxxid_stats *s);

void clear_uid_cache(void);

typedef void (*view_loop)(void);
typedef void (*view_init)(void);
typedef void (*view_fini)(void);

inline void view_batch_loop(void);
inline void view_batch_init(void);
inline void view_batch_fini(void);

inline void view_curses_loop(void);
inline void view_curses_init(void);
inline void view_curses_fini(void);

inline unsigned int curses_sleep(unsigned int seconds);

inline e_grtype masked_grtype(int isforward);
inline int masked_sort_by(int isforward);

/* utils.c */

inline int read_cmdline(int pid,char **cmd_long,char **cmd_short);
inline void read_comm(int pid,const char *comm,char **cmd_long,char **cmd_short,char **cmd_comm);

inline int64_t monotime(void);
inline char *u8strpadt(const char *s,ssize_t len);
inline char *esc_low_ascii(const char *p);

typedef void (*pg_cb)(pid_t pid,pid_t tid,struct xxxid_stats_arr *hint1,filter_callback hint2);
inline void pidgen_cb(pg_cb cb,struct xxxid_stats_arr *hint1,filter_callback hint2);


inline int is_a_dir(const char *p);
inline int is_a_file(const char *p);
inline int is_a_process(pid_t tid);

inline double timediff_in_s(uint64_t sta,uint64_t end);
inline uint64_t timediff_in_ms(uint64_t sta,uint64_t end);

/* ioprio.c */

enum {
	IOPRIO_CLASS_NONE,
	IOPRIO_CLASS_RT,
	IOPRIO_CLASS_BE,
	IOPRIO_CLASS_IDLE,
	IOPRIO_CLASS_MAX,
	IOPRIO_CLASS_MIN=IOPRIO_CLASS_RT,
};

enum {
	IOPRIO_WHO_PROCESS=1,
	IOPRIO_WHO_PGRP,
	IOPRIO_WHO_USER
};

enum {
	SORT_BY_TID,
	SORT_BY_PRIO,
	SORT_BY_USER,
	SORT_BY_READ,
	SORT_BY_VFS_READ,
	SORT_BY_WRITE,
	SORT_BY_VFS_WRITE,
	SORT_BY_SWAPIN,
	SORT_BY_IO,
	SORT_BY_GRAPH,
	SORT_BY_COMMAND,
	SORT_BY_MAX
};

enum {
	SORT_DESC,
	SORT_ASC
};

extern const char *str_ioprio_class[];

inline int ioprio_from_sched(int policy,int nice);
inline int get_ioprio(pid_t pid,int sched_prio);
inline const char *str_ioprio(int io_prio);
inline int ioprio_value(int class,int prio);
inline int set_ioprio(int which,int who,int ioprio_class,int ioprio_prio);
inline int ioprio2class(int ioprio);
inline int ioprio2prio(int ioprio);

/* vmstat.c */

inline int get_vm_counters(uint64_t *pgpgin,uint64_t *pgpgou);

/* checks.c */

inline int system_checks(void);

/* arr.c */

inline struct xxxid_stats_arr *arr_alloc(void);
inline int arr_add(struct xxxid_stats_arr *a,struct xxxid_stats *s);
inline int arr_append(struct xxxid_stats_arr *a,struct xxxid_stats *s);
inline void arr_finalize(struct xxxid_stats_arr *a);
inline int arr_merge(struct xxxid_stats_arr *a,struct xxxid_stats **items,int cnt);
inline struct xxxid_stats *arr_find(struct xxxid_stats_arr *pa,pid_t tid);
inline void arr_free(struct xxxid_stats_arr *pa);
inline void arr_free_noitem(struct xxxid_stats_arr *pa);
inline void arr_sort(struct xxxid_stats_arr *pa,int (*cb)(const void *a,const void *b),int (*keep)(const struct xxxid_stats *s));

#define HEADER1_FORMAT "  Total DISK READ: %7.2f %s%s |   Total DISK WRITE: %7.2f %s%s"
#define HEADER2_FORMAT "Current DISK READ: %7.2f %s%s | Current DISK WRITE: %7.2f %s%s"

inline void calc_total(struct xxxid_stats_arr *cs,uint64_t *read,uint64_t *write);
inline void calc_a_total(struct act_stats *act,uint64_t *read,uint64_t *write);
inline void humanize_val(double *value,char *str,int allow_accum);
inline int iotop_sort_cb(const void *a,const void *b);
inline void iotop_sort(struct xxxid_stats_arr *a,int agg,int (*keep)(const struct xxxid_stats *s));
inline uint64_t rrv(uint64_t to,uint64_t from);
inline int same_task(const struct xxxid_stats *c,const struct xxxid_stats *p);
inline uint64_t accum_bytes(const struct xxxid_stats *s,e_accum v,int agg,int life);
inline uint64_t accum_src(const struct xxxid_stats *s,e_accum v,int agg,uint64_t *age);
inline double accum_val(const struct xxxid_stats *s,e_accum v,int agg);
inline int create_diff(struct xxxid_stats_arr *cs,struct xxxid_stats_arr *ps,uint64_t cyc_ms,uint64_t ts_c,int keep_exited);
inline int count_visible(struct xxxid_stats_arr *cs,filter_callback_w cb,int width);
inline int value2scale(uint64_t val,uint64_t mx);
inline double delay_pc(uint64_t ns,double time_s);
inline uint64_t hist_sum(const struct xxxid_hist *h,const uint64_t *a,int cnt);
inline int hist_sum8(const struct xxxid_hist *h,const uint8_t *a,int cnt);
inline void hist_window(struct xxxid_stats_arr *cs,int w);
inline int filter1(struct xxxid_stats *s);

/* delayacct.c */

inline int has_task_delayacct(void);
inline int read_task_delayacct(void);
inline int write_task_delayacct(int da);

/* configfile.c */

inline int config_file_load(int *pac,char ***pav);
inline void config_file_free(void);
inline int config_file_save(int to_stdout);

/* main.c */

inline void init_params(void);
inline void init_config(void);

#define mymax(a,b) (((a)>(b))?(a):(b))

#endif // __IOTOP_H__

