#include "iotop.h"

#include <errno.h>
#include <linux/genetlink.h>
#include <linux/netlink.h>
#include <linux/taskstats.h>
#include <proc/readproc.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/*
 * Generic macros for dealing with netlink sockets. Might be duplicated
 * elsewhere. It is recommended that commercial grade applications use
 * libnl or libnetlink and use the interfaces provided by the library
 */
#define GENLMSG_DATA(glh)       ((void *)((char*)NLMSG_DATA(glh) + GENL_HDRLEN))
#define GENLMSG_PAYLOAD(glh)    (NLMSG_PAYLOAD(glh, 0) - GENL_HDRLEN)
#define NLA_DATA(na)            ((void *)((char*)(na) + NLA_HDRLEN))
#define NLA_PAYLOAD(len)        (len - NLA_HDRLEN)

#define MAX_MSG_SIZE 1024
#define MAX_CPUS     32

struct msgtemplate {
    struct nlmsghdr n;
    struct genlmsghdr g;
    char buf[MAX_MSG_SIZE];
};

static int nl_sock = -1;
static int nl_fam_id = 0;

int send_cmd(int sock_fd, __u16 nlmsg_type, __u32 nlmsg_pid,
         __u8 genl_cmd, __u16 nla_type,
         void *nla_data, int nla_len)
{
    struct nlattr *na;
    struct sockaddr_nl nladdr;
    int r, buflen;
    char *buf;

    struct msgtemplate msg;

    msg.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
    msg.n.nlmsg_type = nlmsg_type;
    msg.n.nlmsg_flags = NLM_F_REQUEST;
    msg.n.nlmsg_seq = 0;
    msg.n.nlmsg_pid = nlmsg_pid;
    msg.g.cmd = genl_cmd;
    msg.g.version = 0x1;
    na = (struct nlattr *) GENLMSG_DATA(&msg);
    na->nla_type = nla_type;
    na->nla_len = nla_len + 1 + NLA_HDRLEN;
    memcpy(NLA_DATA(na), nla_data, nla_len);
    msg.n.nlmsg_len += NLMSG_ALIGN(na->nla_len);

    buf = (char *) &msg;
    buflen = msg.n.nlmsg_len ;
    memset(&nladdr, 0, sizeof(nladdr));
    nladdr.nl_family = AF_NETLINK;
    while ((r = sendto(sock_fd, buf, buflen, 0, (struct sockaddr *) &nladdr,
                       sizeof(nladdr))) < buflen) {
        if (r > 0) {
            buf += r;
            buflen -= r;
        } else if (errno != EAGAIN)
            return -1;
    }
    return 0;
}

int get_family_id(int sock_fd)
{
    static char name[256];

    struct {
        struct nlmsghdr n;
        struct genlmsghdr g;
        char buf[256];
    } ans;

    int id = 0;
    struct nlattr *na;
    int rep_len;

    strcpy(name, TASKSTATS_GENL_NAME);
    if (send_cmd(sock_fd, GENL_ID_CTRL, getpid(), CTRL_CMD_GETFAMILY,
                    CTRL_ATTR_FAMILY_NAME, (void *) name,
                    strlen(TASKSTATS_GENL_NAME) + 1)) {
        return 0;
    }

    rep_len = recv(sock_fd, &ans, sizeof(ans), 0);
    if (ans.n.nlmsg_type == NLMSG_ERROR
        || (rep_len < 0) || !NLMSG_OK((&ans.n), rep_len)) {
        return 0;
    }

    na = (struct nlattr *) GENLMSG_DATA(&ans);
    na = (struct nlattr *) ((char *) na + NLA_ALIGN(na->nla_len));
    if (na->nla_type == CTRL_ATTR_FAMILY_ID) {
        id = *(__u16 *) NLA_DATA(na);
    }
    return id;
}

void nl_init(void)
{
    struct sockaddr_nl addr;
    int sock_fd = socket(PF_NETLINK, SOCK_RAW, NETLINK_GENERIC);

    if (sock_fd < 0)
        goto error;

    memset(&addr, 0, sizeof(addr));
    addr.nl_family = AF_NETLINK;

    if (bind(sock_fd, (struct sockaddr *) &addr, sizeof(addr)) < 0)
        goto error;

    nl_sock = sock_fd;
    nl_fam_id = get_family_id(sock_fd);

    return;

error:
    if (sock_fd > -1)
        close(sock_fd);

    fprintf(stderr, "nl_init: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
}

int nl_xxxid_info(pid_t xxxid, int isp, struct xxxid_stats *stats)
{
    if (nl_sock < 0) {
        perror("nl_xxxid_info");
        exit(EXIT_FAILURE);
    }

    int cmd_type = isp ? TASKSTATS_CMD_ATTR_PID : TASKSTATS_CMD_ATTR_TGID;

    if (send_cmd(nl_sock, nl_fam_id, xxxid, TASKSTATS_CMD_GET,
                    cmd_type, &xxxid, sizeof(__u32))) {
        fprintf(stderr, "get_xxxid_info: %s\n", strerror(errno));
        return -1;
    }

    stats->tid = xxxid;

    struct msgtemplate msg;
    int rv = recv(nl_sock, &msg, sizeof(msg), 0);

    if (msg.n.nlmsg_type == NLMSG_ERROR ||
            !NLMSG_OK((&msg.n), rv)) {
        struct nlmsgerr *err = NLMSG_DATA(&msg);
        fprintf(stderr, "fatal reply error, %d\n", err->error);
        return -1;
    }

    rv = GENLMSG_PAYLOAD(&msg.n);

    struct nlattr *na = (struct nlattr *) GENLMSG_DATA(&msg);
    int len = 0;

    while (len < rv) {
        len += NLA_ALIGN(na->nla_len);

        switch (na->nla_type) {
        case TASKSTATS_TYPE_AGGR_TGID:
        case TASKSTATS_TYPE_AGGR_PID:
            {
                int aggr_len = NLA_PAYLOAD(na->nla_len);
                int len2 = 0;

                na = (struct nlattr *) NLA_DATA(na);
                while (len2 < aggr_len) {
                    switch (na->nla_type) {
                    case TASKSTATS_TYPE_STATS:
                        {
                            struct taskstats *ts = NLA_DATA(na);
#define COPY(field) { stats->field = ts->field; }
                            COPY(read_bytes);
                            COPY(write_bytes);
                            COPY(swapin_delay_total);
                            COPY(blkio_delay_total);
#undef COPY
                        }
                        break;
                    }
                    len2 += NLA_ALIGN(na->nla_len);
                    na = (struct nlattr *) ((char *) na + len2);
                }
            }
            break;
        }
        na = (struct nlattr *) ((char *) GENLMSG_DATA(&msg) + len);
    }

    stats->io_prio = get_ioprio(xxxid);

    return 0;
}

void nl_term(void)
{
    if (nl_sock > -1)
        close(nl_sock);
}

void dump_xxxid_stats(struct xxxid_stats *stats)
{
    printf("%i %i SWAPIN: %lu IO: %lu "
           "READ: %lu WRITE: %lu IOPRIO: %s   %s\n",
           stats->tid, stats->euid,
           stats->swapin_delay_total,
           stats->blkio_delay_total, stats->read_bytes,
           stats->write_bytes,
           str_ioprio(stats->io_prio),
           stats->cmdline);
}

void update_stats(proc_t *pi, struct xxxid_stats *s)
{
    const static char unknown[] = "<unknown>";
    const char *cmdline = read_cmdline2(pi->tid);

    s->euid = pi->euid;
    s->cmdline = strdup(cmdline ? cmdline : unknown);
}

void free_stats(struct xxxid_stats *s)
{
    if (s->cmdline)
        free(s->cmdline);

    free(s);
}

void free_stats_chain(struct xxxid_stats *chain)
{
    while (chain) {
        struct xxxid_stats *next = chain->__next;

        free_stats(chain);
        chain = next;
    }
}

struct xxxid_stats *make_stats(proc_t *p, int processes)
{
    struct xxxid_stats *s = malloc(sizeof(struct xxxid_stats));
    memset(s, 0, sizeof(struct xxxid_stats));

    if (nl_xxxid_info(p->tid, processes, s))
        goto error;

    update_stats(p, s);
    return s;

error:
    free_stats(s);
    return NULL;
}

struct xxxid_stats *fetch_data(int processes, filter_callback filter)
{
    PROCTAB *proc = openproc(
            PROC_FILLCOM |
            PROC_FILLUSR
        );

    if (!proc) {
        perror("openproc");
        exit(EXIT_FAILURE);
    }

    struct xxxid_stats *schain = NULL;
    struct xxxid_stats *p = NULL;

#define ADD_TO_CHAIN(s) {\
    if (!schain) {\
        schain = s;\
        p = s;\
    } else {\
        p->__next = s;\
        p = s;\
    }\
}

#define FILTER_ON_CHAIN(s) {\
    if (filter && filter(s))\
        free_stats(s);\
    else\
        ADD_TO_CHAIN(s)\
}

    proc_t pi;
    memset(&pi, 0, sizeof(proc_t));

    while (readproc(proc, &pi)) {
        struct xxxid_stats *s = make_stats(&pi, processes);
        FILTER_ON_CHAIN(s);

        if (!processes) {
            proc_t ti;
            memset(&ti, 0, sizeof(proc_t));

            while (readtask(proc, &pi, &ti)) {
                struct xxxid_stats *k = make_stats(&ti, processes);
                if (k->tid != s->tid)
                    FILTER_ON_CHAIN(k);
            }
        }
    }

#undef ADD_TO_CHAIN
#undef FILTER_ON_CHAIN

    closeproc(proc);
    return schain;
}

