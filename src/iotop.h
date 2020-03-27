#ifndef __IOTOP_H__
#define __IOTOP_H__

#define _GNU_SOURCE
#define _POSIX_C_SOURCE
#define _DEFAULT_SOURCE

#include <sys/types.h>
#include <stdint.h>

#define VERSION "0.1"

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
    int have_o;
};

void nl_init(void);
void nl_term(void);

int nl_xxxid_info(pid_t xxxid, int isp, struct xxxid_stats *stats);
void dump_xxxid_stats(struct xxxid_stats *stats);

typedef int (*filter_callback)(struct xxxid_stats *);

struct xxxid_stats_arr *fetch_data(int processes, filter_callback);
void free_stats(struct xxxid_stats *s);

typedef void (*view_callback)(struct xxxid_stats_arr *current, struct xxxid_stats_arr *prev, struct act_stats *);

void view_batch(struct xxxid_stats_arr *, struct xxxid_stats_arr *, struct act_stats *);
void view_curses(struct xxxid_stats_arr *, struct xxxid_stats_arr *, struct act_stats *);
void view_curses_finish();

typedef int (*how_to_sleep)(unsigned int seconds);
int curses_sleep(unsigned int seconds);

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

const char *xprintf(const char *format, ...);
const char *read_cmdline2(int pid);

struct pidgen *openpidgen(int flags);
void closepidgen(struct pidgen *pg);
int pidgen_next(struct pidgen *pg);

/* ioprio.c */

int get_ioprio(pid_t pid);
const char *str_ioprio(int io_prio);
int set_ioprio(int which, int who, const char *ioprio_class, int ioprio_data);

/* vmstat.c */

int get_vm_counters(uint64_t *pgpgin, uint64_t *pgpgout);

/* checks.c */

int system_checks(void);

/* arr.c */

struct xxxid_stats_arr *arr_alloc(void);
int arr_add(struct xxxid_stats_arr *a, struct xxxid_stats *s);
struct xxxid_stats *arr_find(struct xxxid_stats_arr *pa, pid_t tid);
void arr_free(struct xxxid_stats_arr *pa);
void arr_sort(struct xxxid_stats_arr *pa, int (*cb)(const void *a, const void *b, void *arg), void *arg);

#endif // __IOTOP_H__

