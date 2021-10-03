/* SPDX-License-Identifer: GPL-2.0-or-later

Copyright (C) 2014  Vyacheslav Trushkin
Copyright (C) 2020,2021  Boian Bonev

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

#include <sys/types.h>
#include <stdint.h>

#define VERSION "1.19"

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
		int hidewrite;
		int hideswapin;
		int hideio;
		int hidegraph;
		int hidecmd;
		int sort_by;
		int sort_order;
		int deadx;
	} f;
	int opts[19];
} config_t;

typedef struct {
	int iter;
	int delay;
	int pid;
	int user_id;
} params_t;

extern config_t config;
extern params_t params;
extern int maxpidlen;


#define HISTORY_POS 60
#define HISTORY_CNT (HISTORY_POS*2)

struct xxxid_stats_arr {
	struct xxxid_stats **arr;
	struct xxxid_stats **sor;
	int length;
	int size;
};

struct xxxid_stats {
	pid_t pid;
	pid_t tid;
	uint64_t swapin_delay_total; // nanoseconds
	uint64_t blkio_delay_total; // nanoseconds
	uint64_t read_bytes;
	uint64_t write_bytes;

	double blkio_val;
	double swapin_val;
	double read_val;
	double write_val;
	double read_val_acc;
	double write_val_acc;

	int io_prio;

	int euid;
	char *cmdline1;
	char *cmdline2;
	char *pw_name;

	uint8_t iohist[HISTORY_CNT];
	int exited; // exited>0 shows for how many refresh cycles the process is gone
	// there is no point to keep in memory data for processes exited before HISTORY_CNT cycles
	struct xxxid_stats_arr *threads;
};

// arrays are used both for main process/thread list and for inner thread list belonging to a process
// always start small
#define PROC_LIST_SZ_INI 16
// afterwards grow big
#define PROC_LIST_SZ_INC 2048

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

inline int nl_xxxid_info(pid_t tid,pid_t pid,struct xxxid_stats *stats);

typedef int (*filter_callback)(struct xxxid_stats *);
typedef int (*filter_callback_w)(struct xxxid_stats *,int width);

inline struct xxxid_stats_arr *fetch_data(filter_callback);
inline void free_stats(struct xxxid_stats *s);

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

/* utils.c */

inline char *read_cmdline(int pid,int isshort);

inline int64_t monotime(void);
inline char *u8strpadt(const char *s,ssize_t len);
inline char *esc_low_ascii(char *p);

typedef void (*pg_cb)(pid_t pid,pid_t tid,void *hint1,void *hint2);
inline void pidgen_cb(pg_cb cb,void *hint1,void *hint2);

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
	SORT_BY_PID,
	SORT_BY_PRIO,
	SORT_BY_USER,
	SORT_BY_READ,
	SORT_BY_WRITE,
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

inline int get_ioprio(pid_t pid);
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
inline struct xxxid_stats *arr_find(struct xxxid_stats_arr *pa,pid_t tid);
inline void arr_free(struct xxxid_stats_arr *pa);
inline void arr_free_noitem(struct xxxid_stats_arr *pa);
inline void arr_sort(struct xxxid_stats_arr *pa,int (*cb)(const void *a,const void *b));

#define HEADER1_FORMAT "  Total DISK READ: %7.2f %s%s |   Total DISK WRITE: %7.2f %s%s"
#define HEADER2_FORMAT "Current DISK READ: %7.2f %s%s | Current DISK WRITE: %7.2f %s%s"

inline void calc_total(struct xxxid_stats_arr *cs,double *read,double *write);
inline void calc_a_total(struct act_stats *act,double *read,double *write,double time_s);
inline void humanize_val(double *value,char *str,int allow_accum);
inline int iotop_sort_cb(const void *a,const void *b);
inline int create_diff(struct xxxid_stats_arr *cs,struct xxxid_stats_arr *ps,double time_s,filter_callback_w cb,int width,int *cnt);
inline int value2scale(double val,double mx);
inline int filter1(struct xxxid_stats *s);

#ifndef KEY_CTRL_L
#define KEY_CTRL_L 0x0c
#endif
#ifndef KEY_CTRL_T
#define KEY_CTRL_T 0x14
#endif

/* delayacct.c */

inline int has_task_delayacct(void);
inline int read_task_delayacct(void);
inline int write_task_delayacct(int da);

#endif // __IOTOP_H__

