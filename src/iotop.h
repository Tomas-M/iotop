#ifndef __IOTOP_H__
#define __IOTOP_H__

#define _GNU_SOURCE
#define _POSIX_C_SOURCE
#define _DEFAULT_SOURCE

#include <sys/types.h>
#include <stdint.h>

#define VERSION "1.1"

typedef union
{
    struct _flags
    {
        int batch_mode;
        int only;
        int processes;
        int accumulated;
        int kilobytes;
        int timestamp;
        int quiet;
        int nohelp;
        int fullcmdline;
    } f;
    int opts[8];
} config_t;

typedef struct
{
    int iter;
    int delay;
    int pid;
    int user_id;
} params_t;

extern config_t config;
extern params_t params;


struct xxxid_stats
{
    pid_t tid;
    uint64_t swapin_delay_total;  // nanoseconds
    uint64_t blkio_delay_total;  // nanoseconds
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
    char *cmdline;
    char *pw_name;
};

#define PROC_LIST_SZ_INC 1024

struct xxxid_stats_arr
{
    struct xxxid_stats **arr;
    struct xxxid_stats **sor;
    int length;
    int size;
};

struct act_stats
{
    uint64_t read_bytes;
    uint64_t write_bytes;
    uint64_t read_bytes_o;
    uint64_t write_bytes_o;
    uint64_t ts_c;
    uint64_t ts_o;
    int have_o;
};

inline void nl_init(void);
inline void nl_term(void);

inline int nl_xxxid_info(pid_t xxxid, struct xxxid_stats *stats);

typedef int (*filter_callback)(struct xxxid_stats *);

inline struct xxxid_stats_arr *fetch_data(int processes, filter_callback);
inline void free_stats(struct xxxid_stats *s);

typedef void (*view_callback)(struct xxxid_stats_arr *current, struct xxxid_stats_arr *prev, struct act_stats *);

inline void view_batch(struct xxxid_stats_arr *, struct xxxid_stats_arr *, struct act_stats *);
inline void view_curses(struct xxxid_stats_arr *, struct xxxid_stats_arr *, struct act_stats *);
inline void view_curses_finish();

typedef unsigned int (*how_to_sleep)(unsigned int seconds);
inline unsigned int curses_sleep(unsigned int seconds);

/* utils.c */

enum
{
    PIDGEN_FLAGS_PROC,
    PIDGEN_FLAGS_TASK
};

struct pidgen
{
    void *__proc;
    void *__task;
    int __flags;
};

inline char *read_cmdline2(int pid);

inline struct pidgen *openpidgen(int flags);
inline void closepidgen(struct pidgen *pg);
inline int pidgen_next(struct pidgen *pg);
inline int64_t monotime(void);

/* ioprio.c */

enum {
    IOPRIO_CLASS_NONE,
    IOPRIO_CLASS_RT,
    IOPRIO_CLASS_BE,
    IOPRIO_CLASS_IDLE,
    IOPRIO_CLASS_MAX,
    IOPRIO_CLASS_MIN = IOPRIO_CLASS_RT,
};

enum {
    IOPRIO_WHO_PROCESS = 1,
    IOPRIO_WHO_PGRP,
    IOPRIO_WHO_USER
};

extern const char *str_ioprio_class[];

inline int get_ioprio(pid_t pid);
inline const char *str_ioprio(int io_prio);
inline int ioprio_value(int class, int prio);
inline int set_ioprio(int which, int who, int ioprio_class, int ioprio_prio);
inline int ioprio2class(int ioprio);
inline int ioprio2prio(int ioprio);

/* vmstat.c */

inline int get_vm_counters(uint64_t *pgpgin, uint64_t *pgpgout);

/* checks.c */

inline int system_checks(void);

/* arr.c */

inline struct xxxid_stats_arr *arr_alloc(void);
inline int arr_add(struct xxxid_stats_arr *a, struct xxxid_stats *s);
inline struct xxxid_stats *arr_find(struct xxxid_stats_arr *pa, pid_t tid);
inline void arr_free(struct xxxid_stats_arr *pa);
inline void arr_sort(struct xxxid_stats_arr *pa, int (*cb)(const void *a, const void *b, void *arg), void *arg);

#endif // __IOTOP_H__

