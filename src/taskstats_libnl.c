/* SPDX-License-Identifier: GPL-2.0-or-later

Copyright (C) 2023 Costis Contopoulos

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with this program; if not, write to the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.

*/
#ifdef LIBNL

#include "iotop.h"

#include <stdio.h>
#include <unistd.h>
#include <linux/taskstats.h>
#include <netlink/netlink.h>
#include <netlink/msg.h>
#include <netlink/handlers.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>



typedef struct taskstats taskstats_t;

typedef struct netlink_info_t {
    int id;
    struct nl_sock *sk;
    struct nl_cb *cb;
} netlink_info;

static netlink_info nl;
static taskstats_t ts;

inline int nl_xxxid_info_libnl(pid_t tid,pid_t pid,struct xxxid_stats *stats) {
    // Construct the request message
    struct nl_msg *msg = nlmsg_alloc();
    if (!msg) {
        fprintf(stderr, "Failed to allocate Netlink message\n");
        return -1;
    }
    // ...append the message header
    genlmsg_put(msg,
                NL_AUTO_PORT,
	            NL_AUTO_SEQ,
	            nl.id,
	            0,
	            0,
	            TASKSTATS_CMD_GET,
	            TASKSTATS_GENL_VERSION);
    // ...append the pid attribute
    nla_put_u32(msg, TASKSTATS_CMD_ATTR_PID, tid);
    //nl_msg_dump(msg, stdout);
    int r;
    r = nl_send_auto(nl.sk, msg);
    if (r<0) {
        fprintf(stderr, "Failed to send request for pid %d\n", pid);
        goto getpiderror;
    }

    // Receive and process the response
    r = nl_recvmsgs(nl.sk, nl.cb);
    if (r<0) {
        fprintf(stderr, "Failed to get response for pid %d\n", pid);
        goto getpiderror;
    }

    stats->pid = pid;
    stats->tid = tid;
    #define COPY(field) { stats->field = ts.field; }
    COPY(read_bytes);
    COPY(write_bytes);
    COPY(swapin_delay_total);
    COPY(blkio_delay_total);
    #undef COPY
    stats->euid=ts.ac_uid;


    nlmsg_free(msg);
    return 0;

getpiderror:
        nlmsg_free(msg);
        return -1;
}

int parse_taskstats_callback(struct nl_msg *msg, void *arg) {

    struct nlmsghdr *nlh = nlmsg_hdr(msg);
    struct genlmsghdr *gnlh = nlmsg_data(nlh);

	struct nlattr *tb[TASKSTATS_TYPE_MAX + 1];

	nla_parse(tb,
	          TASKSTATS_TYPE_MAX,
	          genlmsg_attrdata(gnlh, 0),
	          genlmsg_attrlen(gnlh, 0),
	          NULL);

    struct nlattr *aggr_type = NULL;

    if (tb[TASKSTATS_TYPE_AGGR_PID])
        aggr_type = tb[TASKSTATS_TYPE_AGGR_PID];
    else if (tb[TASKSTATS_TYPE_AGGR_TGID])
        aggr_type = tb[TASKSTATS_TYPE_AGGR_TGID];

    if (!aggr_type)
        return NL_SKIP;

    if (nla_parse_nested(tb, TASKSTATS_TYPE_MAX, aggr_type, NULL)) {
        fprintf(stderr, "Failed to parse nested 'stats' attribute \n");
        return NL_SKIP;
    }

    taskstats_t *p_ts = nla_data(tb[TASKSTATS_TYPE_STATS]);
    memcpy(arg, p_ts, sizeof(taskstats_t));

    return NL_OK;
}

inline void nl_init_libnl(void) {
    nl.sk = nl_socket_alloc();
    if (!nl.sk) {
        fprintf(stderr, "Failed to allocate Netlink socket\n");
        exit(EXIT_FAILURE);
    }

    if (genl_connect(nl.sk)) {
        fprintf(stderr, "Failled to connect to Nelink socket\n");
        goto nliniterror;
    }

    nl.id = genl_ctrl_resolve (nl.sk, TASKSTATS_GENL_NAME);
    if (nl.id < 0) {
        fprintf(stderr, TASKSTATS_GENL_NAME " Generic Netlink family not found\n");
        goto nliniterror;
    }
    // disable ACK for the following taskset requests
    nl_socket_disable_auto_ack(nl.sk);

    nl.cb = nl_cb_alloc(NL_CB_DEFAULT);
    nl_cb_set(nl.cb, NL_CB_VALID, NL_CB_CUSTOM, parse_taskstats_callback, &ts);

    return;

nliniterror:
    nl_socket_free(nl.sk);
    exit(EXIT_FAILURE);

}

inline void nl_fini_libnl(void) {
    nl_socket_free(nl.sk);
}

#endif // LIBNL sentry

