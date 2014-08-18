#ifndef __IOTOP_H__
#define __IOTOP_H__

#define _BSD_SOURCE

#include <sys/types.h>
#include <stdint.h>

#define VERSION "0.1"

typedef union {
    struct _flags {
        int batch_mode;
        int only;
        int processes;
        int accumulated;
        int kilobytes;
        int timestamp;
        int quite;
    } f;
    int opts[7];
} config_t;

typedef struct {
    int iter;
    int delay;
    int pid;
    int user_id;
} params_t;

extern config_t config;
extern params_t params;


struct xxxid_stats {
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

    void *__next;
};

void nl_init(void);
void nl_term(void);

int nl_xxxid_info(pid_t xxxid, int isp, struct xxxid_stats *stats);
void dump_xxxid_stats(struct xxxid_stats *stats);

typedef int (*filter_callback)(struct xxxid_stats *);

struct xxxid_stats* fetch_data(int processes, filter_callback);
void free_stats_chain(struct xxxid_stats *chain);

typedef void (*view_callback)(struct xxxid_stats *current, struct xxxid_stats *prev);

void view_batch(struct xxxid_stats *, struct xxxid_stats *);
void view_curses(struct xxxid_stats *, struct xxxid_stats *);
void view_curses_finish();

typedef int (*how_to_sleep)(unsigned int seconds);
int curses_sleep(unsigned int seconds);

/* utils.c */

const char *xprintf(const char *format, ...);
const char *file2str(const char *filepath);

/* ioprio.h */

int get_ioprio(pid_t pid);
const char *str_ioprio(int io_prio);

#endif // __IOTOP_H__

